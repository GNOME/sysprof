/*
 * sysprof-live-unwinder.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <fcntl.h>
#include <unistd.h>

#include <linux/perf_event.h>

#include <glib/gstdio.h>

#include "sysprof-live-process.h"
#include "sysprof-live-unwinder.h"
#include "sysprof-maps-parser-private.h"

struct _SysprofLiveUnwinder
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  GHashTable           *live_pids_by_pid;
};

G_DEFINE_FINAL_TYPE (SysprofLiveUnwinder, sysprof_live_unwinder, G_TYPE_OBJECT)

enum {
  CLOSED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static char *
sysprof_live_unwinder_read_file (SysprofLiveUnwinder *self,
                                 const char          *path,
                                 gboolean             insert_into_capture)
{
  gint64 when = SYSPROF_CAPTURE_CURRENT_TIME;
  char *contents = NULL;
  gsize len = 0;
  gsize offset = 0;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));
  g_assert (self->writer != NULL);

  if (!g_file_get_contents (path, &contents, &len, NULL))
    return NULL;

  if (insert_into_capture)
    {
      while (len > 0)
        {
          gsize this_write = MIN (len, 4096*4);

          if (!sysprof_capture_writer_add_file (self->writer,
                                                when,
                                                -1,
                                                -1,
                                                path,
                                                this_write == len,
                                                (const guint8 *)&contents[offset], this_write))
            break;

          len -= this_write;
          offset += this_write;
        }
    }

  return contents;
}

static char *
sysprof_live_unwinder_read_pid_file (SysprofLiveUnwinder *self,
                                     GPid                 pid,
                                     const char          *path_part,
                                     gboolean             insert_into_capture)
{
  g_autofree char *path = NULL;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));
  g_assert (self->writer != NULL);

  path = g_strdup_printf ("/proc/%d/%s", pid, path_part);

  return sysprof_live_unwinder_read_file (self, path, insert_into_capture);
}

static SysprofLiveProcess *
sysprof_live_unwinder_find_pid (SysprofLiveUnwinder *self,
                                GPid                 pid,
                                gboolean             send_comm)
{
  SysprofLiveProcess *live_pid;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  if (pid < 0)
    return NULL;

  if (!(live_pid = g_hash_table_lookup (self->live_pids_by_pid, GINT_TO_POINTER (pid))))
    {
      gint64 now = SYSPROF_CAPTURE_CURRENT_TIME;

      live_pid = sysprof_live_process_new (pid);

      g_hash_table_replace (self->live_pids_by_pid, GINT_TO_POINTER (pid), live_pid);

      if (send_comm)
        {
          g_autofree char *path = g_strdup_printf ("/proc/%d/comm", pid);
          g_autofree char *comm = NULL;
          gsize len;

          if (g_file_get_contents (path, &comm, &len, NULL))
            {
              g_autofree char *tmp = comm;
              comm = g_strstrip (g_utf8_make_valid (tmp, len));
              sysprof_capture_writer_add_process (self->writer, now, -1, pid, comm);
            }
        }

      if (sysprof_live_process_is_active (live_pid))
        {
          g_autofree char *mountinfo = sysprof_live_unwinder_read_pid_file (self, pid, "mountinfo", TRUE);
          g_autofree char *maps = sysprof_live_unwinder_read_pid_file (self, pid, "maps", FALSE);

          if (maps != NULL)
            {
              SysprofMapsParser maps_parser;
              guint64 begin, end, offset, inode;
              char *filename;

              sysprof_maps_parser_init (&maps_parser, maps, -1);

              while (sysprof_maps_parser_next (&maps_parser, &begin, &end, &offset, &inode, &filename))
                {
                  sysprof_live_process_add_map (live_pid, begin, end, offset, inode, filename);
                  sysprof_capture_writer_add_map (self->writer, now, -1, pid,
                                                  begin, end, offset,
                                                  inode, filename);
                  g_free (filename);
                }
            }
        }
    }

  return live_pid;
}

static void
sysprof_live_unwinder_mine_pids (SysprofLiveUnwinder *self)
{
  g_autoptr(GDir) dir = NULL;
  const char *name;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));
  g_assert (self->writer != NULL);

  if (!(dir = g_dir_open ("/proc", 0, NULL)))
    return;

  while ((name = g_dir_read_name (dir)))
    {
      GPid pid;

      if (!g_ascii_isdigit (*name))
        continue;

      if (!(pid = atoi (name)))
        continue;

      sysprof_live_unwinder_find_pid (self, pid, TRUE);
    }
}

static void
sysprof_live_unwinder_finalize (GObject *object)
{
  SysprofLiveUnwinder *self = (SysprofLiveUnwinder *)object;

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->live_pids_by_pid, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_live_unwinder_parent_class)->finalize (object);
}

static void
sysprof_live_unwinder_class_init (SysprofLiveUnwinderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_live_unwinder_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
sysprof_live_unwinder_init (SysprofLiveUnwinder *self)
{
  self->live_pids_by_pid = g_hash_table_new_full (NULL,
                                                  NULL,
                                                  NULL,
                                                  (GDestroyNotify)sysprof_live_process_unref);
}

SysprofLiveUnwinder *
sysprof_live_unwinder_new (SysprofCaptureWriter *writer,
                           int                   kallsyms_fd)
{
  SysprofLiveUnwinder *self;
  g_autofree char *mounts = NULL;

  g_return_val_if_fail (writer != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_LIVE_UNWINDER, NULL);
  self->writer = sysprof_capture_writer_ref (writer);

  if (kallsyms_fd != -1)
    {
      sysprof_capture_writer_add_file_fd (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          "/proc/kallsyms",
                                          kallsyms_fd);
      close (kallsyms_fd);
    }

  mounts = sysprof_live_unwinder_read_file (self, "/proc/mounts", TRUE);

  sysprof_live_unwinder_mine_pids (self);

  return self;
}

void
sysprof_live_unwinder_seen_process (SysprofLiveUnwinder *self,
                                    gint64               time,
                                    int                  cpu,
                                    GPid                 pid,
                                    const char          *comm)
{
  G_GNUC_UNUSED SysprofLiveProcess *live_pid;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  live_pid = sysprof_live_unwinder_find_pid (self, pid, FALSE);

  sysprof_capture_writer_add_process (self->writer, time, cpu, pid, comm);
}

void
sysprof_live_unwinder_process_exited (SysprofLiveUnwinder *self,
                                      gint64               time,
                                      int                  cpu,
                                      GPid                 pid)
{
  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  sysprof_capture_writer_add_exit (self->writer, time, cpu, pid);
}

void
sysprof_live_unwinder_process_forked (SysprofLiveUnwinder *self,
                                      gint64               time,
                                      int                  cpu,
                                      GPid                 parent_tid,
                                      GPid                 tid)
{
  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  sysprof_capture_writer_add_fork (self->writer, time, cpu, parent_tid, tid);
}

void
sysprof_live_unwinder_track_mmap (SysprofLiveUnwinder   *self,
                                  gint64                 time,
                                  int                    cpu,
                                  GPid                   pid,
                                  SysprofCaptureAddress  begin,
                                  SysprofCaptureAddress  end,
                                  SysprofCaptureAddress  offset,
                                  guint64                inode,
                                  const char            *filename,
                                  const char            *build_id)
{
  G_GNUC_UNUSED SysprofLiveProcess *live_pid;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  live_pid = sysprof_live_unwinder_find_pid (self, pid, TRUE);

  if (build_id != NULL)
    sysprof_capture_writer_add_map_with_build_id (self->writer, time, cpu, pid,
                                                  begin, end, offset,
                                                  inode, filename,
                                                  build_id);
  else
    sysprof_capture_writer_add_map (self->writer, time, cpu, pid,
                                    begin, end, offset,
                                    inode, filename);

  sysprof_live_process_add_map (live_pid, begin, end, offset, inode, filename);
}

void
sysprof_live_unwinder_process_sampled (SysprofLiveUnwinder         *self,
                                       gint64                       time,
                                       int                          cpu,
                                       GPid                         pid,
                                       GPid                         tid,
                                       const SysprofCaptureAddress *addresses,
                                       guint                        n_addresses)
{
  G_GNUC_UNUSED SysprofLiveProcess *live_pid;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));

  live_pid = sysprof_live_unwinder_find_pid (self, pid, TRUE);

  sysprof_capture_writer_add_sample (self->writer, time, cpu, pid, tid,
                                     addresses, n_addresses);
}

void
sysprof_live_unwinder_process_sampled_with_stack (SysprofLiveUnwinder         *self,
                                                  gint64                       time,
                                                  int                          cpu,
                                                  GPid                         pid,
                                                  GPid                         tid,
                                                  const SysprofCaptureAddress *addresses,
                                                  guint                        n_addresses,
                                                  const guint8                *stack,
                                                  guint64                      stack_size,
                                                  guint64                      stack_dyn_size,
                                                  guint64                      abi,
                                                  const guint64               *registers,
                                                  guint                        n_registers)
{
  SysprofLiveProcess *live_pid;
  SysprofCaptureAddress unwound[256];
  gboolean found_user = FALSE;
  guint pos;

  g_assert (SYSPROF_IS_LIVE_UNWINDER (self));
  g_assert (stack != NULL);
  g_assert (stack_dyn_size <= stack_size);

  if (stack_dyn_size == 0 || n_addresses >= G_N_ELEMENTS (unwound))
    {
      sysprof_live_unwinder_process_sampled (self, time, cpu, pid, tid, addresses, n_addresses);
      return;
    }

  /* We seem to get values > stack_size, which perhaps indicates we can
   * sometimes discover if we would not have gotten enough stack to unwind.
   */
  stack_dyn_size = MIN (stack_dyn_size, stack_size);

  live_pid = sysprof_live_unwinder_find_pid (self, pid, TRUE);

  /* Copy addresses over (which might be kernel, context-switch, etc until
   * we get to the PERF_CONTEXT_USER. We'll decode the stack right into the
   * location after that.
   */
  for (pos = 0; pos < n_addresses; pos++)
    {
      unwound[pos] = addresses[pos];

      if (addresses[pos] == PERF_CONTEXT_USER)
        {
          found_user = TRUE;
          break;
        }
    }

  /* If we didn't find a user context (but we have a stack size) synthesize
   * the PERF_CONTEXT_USER now.
   */
  if (!found_user && pos < G_N_ELEMENTS (unwound))
    unwound[pos++] = PERF_CONTEXT_USER;

  /* Now request the live process unwind the user-space stack */
  if (pos < G_N_ELEMENTS (unwound))
    {
      guint n_unwound;

      n_unwound = sysprof_live_process_unwind (live_pid,
                                               tid,
                                               abi,
                                               stack,
                                               stack_dyn_size,
                                               registers,
                                               n_registers,
                                               &unwound[pos],
                                               G_N_ELEMENTS (unwound) - pos);

      /* Only take DWARF unwind if it was better */
      if (pos + n_unwound > n_addresses)
        {
          addresses = unwound;
          n_addresses = pos + n_unwound;
        }
    }

  sysprof_capture_writer_add_sample (self->writer, time, cpu, pid, tid, addresses, n_addresses);
}
