/* sysprof-perf-map.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysprof-instrument-private.h"
#include "sysprof-perf-map.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#define PERF_MAP_MAX_SIZE (128L * 1024L * 1024L)
#define PERF_MAP_OPEN_ATTEMPTS 20
#define PERF_MAP_OPEN_DELAY_USEC (G_USEC_PER_SEC / 20)

typedef struct _PerfMapFile
{
  int fd;
  int pid;
} PerfMapFile;

typedef struct _OpenMap
{
  SysprofPerfMap *self;
  SysprofRecording *recording;
  GDBusConnection *connection;
  guint64 generation;
  int pid;
  guint is_initial : 1;
  guint allow_privileged : 1;
} OpenMap;

typedef struct _Record
{
  SysprofPerfMap *self;
  SysprofRecording *recording;
  DexFuture *cancellable;
} Record;

struct _SysprofPerfMap
{
  SysprofInstrument parent_instance;
  GDBusConnection *connection;
  GHashTable *files;
  GHashTable *seen;
  GPtrArray *pending;
  GMutex mutex;
  guint64 next_generation;
  guint stopping : 1;
};

struct _SysprofPerfMapClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofPerfMap, sysprof_perf_map, SYSPROF_TYPE_INSTRUMENT)

static void
perf_map_file_free (gpointer data)
{
  PerfMapFile *file = data;

  if (file->fd != -1)
    close (file->fd);

  g_free (file);
}

static PerfMapFile *
perf_map_file_dup (const PerfMapFile *file)
{
  PerfMapFile *copy;

  g_assert (file != NULL);
  g_assert (file->fd != -1);

  copy = g_new0 (PerfMapFile, 1);
  copy->fd = dup (file->fd);
  copy->pid = file->pid;

  if (copy->fd == -1)
    g_clear_pointer (&copy, g_free);

  return copy;
}

static void
open_map_free (gpointer data)
{
  OpenMap *open_map = data;

  g_clear_object (&open_map->self);
  g_clear_object (&open_map->recording);
  g_clear_object (&open_map->connection);
  g_free (open_map);
}

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->self);
  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static gboolean
has_generation (SysprofPerfMap *self,
                int             pid,
                guint64         generation)
{
  const guint64 *current;

  g_assert (SYSPROF_IS_PERF_MAP (self));
  g_assert (pid > 0);
  g_assert (generation > 0);

  current = g_hash_table_lookup (self->seen, GINT_TO_POINTER (pid));

  return current != NULL && *current == generation;
}

static DexFuture *
open_map_completed_cb (DexFuture *completed,
                       gpointer   user_data)
{
  SysprofPerfMap *self = user_data;

  g_assert (DEX_IS_FUTURE (completed));
  g_assert (SYSPROF_IS_PERF_MAP (self));

  g_mutex_lock (&self->mutex);
  g_ptr_array_remove (self->pending, completed);
  g_mutex_unlock (&self->mutex);

  return NULL;
}

static int
parse_namespace_pid (GBytes *status,
                     int     fallback)
{
  g_autofree char *contents = NULL;
  g_auto(GStrv) lines = NULL;
  const char *data;
  gsize len = 0;

  g_assert (status != NULL);
  g_assert (fallback > 0);

  data = g_bytes_get_data (status, &len);
  contents = g_strndup (data, len);
  lines = g_strsplit (contents, "\n", 0);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      const char *iter;
      int last = 0;

      if (!g_str_has_prefix (lines[i], "NSpid:"))
        continue;

      iter = lines[i] + strlen ("NSpid:");

      while (*iter != 0)
        {
          char *endptr = NULL;
          gint64 value;

          while (g_ascii_isspace (*iter))
            iter++;

          if (*iter == 0)
            break;

          value = g_ascii_strtoll (iter, &endptr, 10);
          if (endptr == iter || value <= 0 || value > G_MAXINT)
            break;

          last = value;
          iter = endptr;
        }

      if (last > 0)
        return last;
    }

  return fallback;
}

static int
get_proc_fd (GDBusConnection  *connection,
             const char       *path,
             GError          **error)
{
  int fd;

  g_assert (!connection || G_IS_DBUS_CONNECTION (connection));
  g_assert (path != NULL);

  if (-1 != (fd = open (path, (O_RDONLY | O_CLOEXEC | O_NOFOLLOW))))
    return fd;

  if (connection != NULL)
    {
      g_autoptr(GUnixFDList) out_fd_list = NULL;
      g_autoptr(DexFuture) future = NULL;
      g_autoptr(GVariant) reply = NULL;
      int handle;

      future = dex_dbus_connection_call_with_unix_fd_list (connection,
                                                           "org.gnome.Sysprof3",
                                                           "/org/gnome/Sysprof3",
                                                           "org.gnome.Sysprof3.Service",
                                                           "GetProcFd",
                                                           g_variant_new ("(^ay)", path),
                                                           G_VARIANT_TYPE ("(h)"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           G_MAXUINT,
                                                           NULL);

      if (!dex_await (dex_ref (future), error))
        return -1;

      g_assert (DEX_IS_FUTURE_SET (future));

      if (!(out_fd_list = dex_await_object (
              dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 1)), error)) ||
          !(reply = dex_await_variant (
              dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0)), error)))
        return -1;

      g_variant_get (reply, "(h)", &handle);

      return g_unix_fd_list_get (out_fd_list, handle, error);
    }

  return -1;
}

static gboolean
looks_like_python (const char *comm)
{
  g_autofree char *lower = NULL;

  if (comm == NULL)
    return FALSE;

  lower = g_ascii_strdown (comm, -1);

  return strstr (lower, "python") != NULL;
}

static void
snapshot_file (SysprofRecording  *recording,
               const PerfMapFile *file)
{
  g_autofree char *capture_path = NULL;
  g_autofree char *contents = NULL;
  struct stat st;
  gsize position = 0;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (file != NULL);
  g_assert (file->fd != -1);
  g_assert (file->pid > 0);

  if (fstat (file->fd, &st) != 0 ||
      !S_ISREG (st.st_mode) ||
      st.st_size <= 0 ||
      st.st_size > PERF_MAP_MAX_SIZE)
    return;

  contents = g_malloc (st.st_size);

  while (position < st.st_size)
    {
      ssize_t n_read = pread (file->fd,
                              contents + position,
                              st.st_size - position,
                              position);

      if (n_read < 0 && errno == EINTR)
        continue;

      if (n_read <= 0)
        break;

      position += n_read;
    }

  if (position == 0)
    return;

  capture_path = g_strdup_printf ("__perf_maps__/%d.map", file->pid);
  _sysprof_recording_add_file_data (recording,
                                    capture_path,
                                    contents,
                                    position,
                                    TRUE);
}

static DexFuture *
sysprof_perf_map_open_fiber (gpointer user_data)
{
  OpenMap *open_map = user_data;
  g_autoptr(GBytes) status = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *status_contents = NULL;
  g_autofree char *status_path = NULL;
  g_autofree char *map_path = NULL;
  PerfMapFile *file = NULL;
  gsize status_len = 0;
  guint n_attempts;
  int namespace_pid;

  g_assert (open_map != NULL);
  g_assert (SYSPROF_IS_PERF_MAP (open_map->self));
  g_assert (SYSPROF_IS_RECORDING (open_map->recording));
  g_assert (open_map->pid > 0);

  namespace_pid = open_map->pid;
  status_path = g_strdup_printf ("/proc/%d/status", open_map->pid);

  if (g_file_get_contents (status_path, &status_contents, &status_len, NULL))
    status = g_bytes_new_take (g_steal_pointer (&status_contents), status_len);
  else if (open_map->allow_privileged && open_map->connection != NULL)
    status = dex_await_boxed (sysprof_get_proc_file_bytes (open_map->connection,
                                                           status_path),
                              NULL);

  if (status != NULL)
    namespace_pid = parse_namespace_pid (status, namespace_pid);

  map_path = g_strdup_printf ("/proc/%d/root/tmp/perf-%d.map",
                              open_map->pid,
                              namespace_pid);

  n_attempts = (open_map->is_initial && !open_map->allow_privileged)
                 ? 1
                 : PERF_MAP_OPEN_ATTEMPTS;

  for (guint attempt = 0; attempt < n_attempts; attempt++)
    {
      int fd;

      g_clear_error (&error);

      if (-1 != (fd = get_proc_fd (open_map->allow_privileged ? open_map->connection : NULL,
                                   map_path,
                                   &error)))
        {
          file = g_new0 (PerfMapFile, 1);
          file->fd = fd;
          file->pid = open_map->pid;
          break;
        }

      if (attempt + 1 < n_attempts)
        dex_await (dex_timeout_new_usec (PERF_MAP_OPEN_DELAY_USEC), NULL);
    }

  if (file != NULL)
    {
      PerfMapFile *copy = NULL;

      g_mutex_lock (&open_map->self->mutex);
      if (has_generation (open_map->self, open_map->pid, open_map->generation) &&
          !g_hash_table_contains (open_map->self->files,
                                  GINT_TO_POINTER (open_map->pid)))
        {
          g_hash_table_insert (open_map->self->files,
                               GINT_TO_POINTER (open_map->pid),
                               file);
          copy = perf_map_file_dup (file);
          file = NULL;
        }
      g_mutex_unlock (&open_map->self->mutex);

      if (copy != NULL)
        snapshot_file (open_map->recording, copy);

      g_clear_pointer (&copy, perf_map_file_free);
      g_clear_pointer (&file, perf_map_file_free);
    }
  else
    {
      /* A later COMM event may give us another opportunity after an early
       * fork notification raced with exec or Python initialization.
       */
      g_mutex_lock (&open_map->self->mutex);
      if (has_generation (open_map->self, open_map->pid, open_map->generation))
        g_hash_table_remove (open_map->self->seen, GINT_TO_POINTER (open_map->pid));
      g_mutex_unlock (&open_map->self->mutex);
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_perf_map_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  g_autoptr(GPtrArray) pending = NULL;
  g_autoptr(GPtrArray) files = NULL;
  GHashTableIter iter;
  gpointer value;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_PERF_MAP (record->self));
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  dex_await (dex_ref (record->cancellable), NULL);

  pending = g_ptr_array_new_with_free_func (dex_unref);
  g_mutex_lock (&record->self->mutex);
  record->self->stopping = TRUE;
  for (guint i = 0; i < record->self->pending->len; i++)
    g_ptr_array_add (pending, dex_ref (g_ptr_array_index (record->self->pending, i)));
  g_mutex_unlock (&record->self->mutex);

  if (pending->len > 0)
    dex_await (dex_future_allv ((DexFuture **)pending->pdata, pending->len), NULL);

  files = g_ptr_array_new_with_free_func (perf_map_file_free);
  g_mutex_lock (&record->self->mutex);
  g_hash_table_iter_init (&iter, record->self->files);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      PerfMapFile *copy;

      if ((copy = perf_map_file_dup (value)))
        g_ptr_array_add (files, copy);
    }
  g_mutex_unlock (&record->self->mutex);

  for (guint i = 0; i < files->len; i++)
    snapshot_file (record->recording, g_ptr_array_index (files, i));

  return dex_future_new_for_boolean (TRUE);
}

static char **
sysprof_perf_map_list_required_policy (SysprofInstrument *instrument)
{
  static const char *policy[] = {"org.gnome.sysprof3.profile", NULL};

  g_assert (SYSPROF_IS_PERF_MAP (instrument));

  return g_strdupv ((char **)policy);
}

static void
sysprof_perf_map_set_connection (SysprofInstrument *instrument,
                                 GDBusConnection   *connection)
{
  SysprofPerfMap *self = (SysprofPerfMap *)instrument;

  g_assert (SYSPROF_IS_PERF_MAP (self));
  g_assert (!connection || G_IS_DBUS_CONNECTION (connection));

  g_set_object (&self->connection, connection);
}

static DexFuture *
sysprof_perf_map_process_started (SysprofInstrument *instrument,
                                  SysprofRecording  *recording,
                                  int                pid,
                                  const char        *comm,
                                  gboolean           is_initial)
{
  SysprofPerfMap *self = (SysprofPerfMap *)instrument;
  DexFuture *open_future;
  DexFuture *future;
  OpenMap *open_map;
  guint64 *generation;

  g_assert (SYSPROF_IS_PERF_MAP (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (pid > 0);

  g_mutex_lock (&self->mutex);
  if (self->stopping ||
      g_hash_table_contains (self->seen, GINT_TO_POINTER (pid)))
    {
      g_mutex_unlock (&self->mutex);
      return dex_future_new_for_boolean (TRUE);
    }
  generation = g_new (guint64, 1);
  *generation = ++self->next_generation;
  g_hash_table_insert (self->seen, GINT_TO_POINTER (pid), generation);

  open_map = g_new0 (OpenMap, 1);
  open_map->self = g_object_ref (self);
  open_map->recording = g_object_ref (recording);
  open_map->connection = self->connection ? g_object_ref (self->connection) : NULL;
  open_map->generation = *generation;
  open_map->pid = pid;
  open_map->is_initial = !!is_initial;
  open_map->allow_privileged = looks_like_python (comm);

  open_future = dex_scheduler_spawn (NULL,
                                     0,
                                     sysprof_perf_map_open_fiber,
                                     open_map,
                                     open_map_free);

  g_ptr_array_add (self->pending, dex_ref (open_future));
  g_mutex_unlock (&self->mutex);

  future = dex_future_finally (open_future,
                               open_map_completed_cb,
                               g_object_ref (self),
                               g_object_unref);

  return future;
}

static void
sysprof_perf_map_process_exited (SysprofInstrument *instrument,
                                 SysprofRecording  *recording,
                                 int                pid)
{
  SysprofPerfMap *self = (SysprofPerfMap *)instrument;
  PerfMapFile *file = NULL;

  g_assert (SYSPROF_IS_PERF_MAP (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (pid > 0);

  g_mutex_lock (&self->mutex);
  g_hash_table_remove (self->seen, GINT_TO_POINTER (pid));
  g_hash_table_steal_extended (self->files,
                               GINT_TO_POINTER (pid),
                               NULL,
                               (gpointer *)&file);
  g_mutex_unlock (&self->mutex);

  if (file != NULL)
    snapshot_file (recording, file);

  g_clear_pointer (&file, perf_map_file_free);
}

static DexFuture *
sysprof_perf_map_record (SysprofInstrument *instrument,
                         SysprofRecording  *recording,
                         GCancellable      *cancellable)
{
  SysprofPerfMap *self = (SysprofPerfMap *)instrument;
  Record *record;

  g_assert (SYSPROF_IS_PERF_MAP (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->self = g_object_ref (self);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              sysprof_perf_map_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_perf_map_dispose (GObject *object)
{
  SysprofPerfMap *self = (SysprofPerfMap *)object;

  g_clear_object (&self->connection);

  G_OBJECT_CLASS (sysprof_perf_map_parent_class)->dispose (object);
}

static void
sysprof_perf_map_finalize (GObject *object)
{
  SysprofPerfMap *self = (SysprofPerfMap *)object;

  g_clear_pointer (&self->files, g_hash_table_unref);
  g_clear_pointer (&self->seen, g_hash_table_unref);
  g_clear_pointer (&self->pending, g_ptr_array_unref);
  g_mutex_clear (&self->mutex);

  G_OBJECT_CLASS (sysprof_perf_map_parent_class)->finalize (object);
}

static void
sysprof_perf_map_class_init (SysprofPerfMapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_perf_map_dispose;
  object_class->finalize = sysprof_perf_map_finalize;

  instrument_class->list_required_policy = sysprof_perf_map_list_required_policy;
  instrument_class->set_connection = sysprof_perf_map_set_connection;
  instrument_class->process_started = sysprof_perf_map_process_started;
  instrument_class->process_exited = sysprof_perf_map_process_exited;
  instrument_class->record = sysprof_perf_map_record;
}

static void
sysprof_perf_map_init (SysprofPerfMap *self)
{
  g_mutex_init (&self->mutex);
  self->files = g_hash_table_new_full (NULL, NULL, NULL, perf_map_file_free);
  self->seen = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  self->pending = g_ptr_array_new_with_free_func (dex_unref);
}

/**
 * sysprof_perf_map_new:
 *
 * Creates an instrument which collects Linux perf-map files for processes
 * observed during the recording.
 *
 * Returns: (transfer full): a new #SysprofPerfMap
 */
SysprofInstrument *
sysprof_perf_map_new (void)
{
  return g_object_new (SYSPROF_TYPE_PERF_MAP, NULL);
}
