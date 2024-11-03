/* helpers.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "helpers"

#include "config.h"

#include <fcntl.h>
#include <gio/gio.h>
#include <errno.h>
#ifdef __linux__
# include <linux/perf_event.h>
# include <sys/syscall.h>
#endif
#include <string.h>
#include <unistd.h>

#include "helpers.h"

#ifdef __linux__
static gboolean
linux_list_processes (gint32 **processes,
                      gsize   *n_processes)
{
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GArray) pids = NULL;
  const gchar *name;

  if (!(dir = g_dir_open ("/proc/", 0, NULL)))
    return FALSE;

  pids = g_array_new (FALSE, FALSE, sizeof (gint32));

  while ((name = g_dir_read_name (dir)))
    {
      if (g_ascii_isalnum (*name))
        {
          gchar *endptr = NULL;
          gint64 val = g_ascii_strtoll (name, &endptr, 10);

          if (endptr != NULL && *endptr == 0 && val < G_MAXINT && val >= 0)
            {
              gint32 v32 = val;
              g_array_append_val (pids, v32);
            }
        }
    }

  *n_processes = pids->len;
  *processes = (gint32 *)(gpointer)g_array_free (g_steal_pointer (&pids), FALSE);

  return TRUE;
}
#endif

gboolean
helpers_list_processes (gint32 **processes,
                        gsize   *n_processes)
{
  g_return_val_if_fail (processes != NULL, FALSE);
  g_return_val_if_fail (n_processes != NULL, FALSE);

  *processes = NULL;
  *n_processes = 0;

#ifdef __linux__
  return linux_list_processes (processes, n_processes);
#else
  return FALSE;
#endif
}

#ifdef __linux__
static int
_perf_event_open (struct perf_event_attr *attr,
                  pid_t                   pid,
                  int                     cpu,
                  int                     group_fd,
                  unsigned long           flags)
{
  g_assert (attr != NULL);

  /* Quick sanity check */
  if (attr->sample_period < 100000 && attr->type != PERF_TYPE_TRACEPOINT)
    return -EINVAL;

  return syscall (__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}
#endif

gboolean
helpers_perf_event_open (GVariant *options,
                         gint32    pid,
                         gint32    cpu,
                         gint      group_fd,
                         guint64   flags,
                         gint     *out_fd)
{
#ifndef __linux__
  *out_fd = -1;
  return FALSE;
#else
  struct perf_event_attr attr = {0};
  GVariantIter iter;
  GVariant *value;
  char *key;
  gint32 disabled = 0;
  gint32 wakeup_events = 149;
  gint32 type = 0;
  guint64 sample_period = 0;
  guint64 sample_type = 0;
  guint64 config = 0;
  guint64 sample_regs_user = 0;
  guint sample_stack_user = 0;
  int clockid = CLOCK_MONOTONIC;
  int comm = 0;
  int mmap_ = 0;
  int mmap2 = 0;
  int build_id = 0;
  int task = 0;
  int exclude_idle = 0;
  int use_clockid = 0;
  int sample_id_all = 0;

  g_assert (out_fd != NULL);

  *out_fd = -1;

  g_variant_iter_init (&iter, options);

  while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
    {
      if (FALSE) {}
      else if (strcmp (key, "disabled") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          disabled = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "wakeup_events") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            goto bad_arg;
          wakeup_events = g_variant_get_uint32 (value);
        }
      else if (strcmp (key, "sample_id_all") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          sample_id_all = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "build_id") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          build_id = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "clockid") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
            goto bad_arg;
          clockid = g_variant_get_int32 (value);
        }
      else if (strcmp (key, "comm") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          comm = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "exclude_idle") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          exclude_idle = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "mmap") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          mmap_ = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "mmap2") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          mmap2 = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "config") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          config = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "sample_period") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          sample_period = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "sample_type") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          sample_type = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "task") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          task = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "type") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            goto bad_arg;
          type = g_variant_get_uint32 (value);
        }
      else if (strcmp (key, "use_clockid") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          use_clockid = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "sample_stack_user") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            goto bad_arg;
          sample_stack_user = g_variant_get_uint32 (value);
        }
      else if (strcmp (key, "sample_regs_user") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          sample_regs_user = g_variant_get_uint64 (value);
        }

      continue;

    bad_arg:
      errno = EINVAL;
      return FALSE;
    }

  attr.comm = !!comm;
  attr.config = config;
  attr.disabled = disabled;
  attr.exclude_idle = !!exclude_idle;
  attr.mmap = !!mmap_;
  attr.mmap2 = !!mmap2;
  attr.build_id = !!build_id;
  attr.sample_id_all = sample_id_all;
  attr.sample_period = sample_period;
  attr.sample_type = sample_type;
  attr.task = !!task;
  attr.type = type;
  attr.wakeup_events = wakeup_events;
  attr.sample_regs_user = sample_regs_user;
  attr.sample_stack_user = sample_stack_user;

#ifdef HAVE_PERF_CLOCKID
  if (!use_clockid || clockid < 0)
    attr.clockid = CLOCK_MONOTONIC;
  else
    attr.clockid = clockid;
  attr.use_clockid = use_clockid;
#endif

  attr.size = sizeof attr;

  errno = 0;
  *out_fd = _perf_event_open (&attr, pid, cpu, group_fd, flags);
  return *out_fd >= 0;
#endif
}

gboolean
helpers_get_proc_file (const gchar  *path,
                       gchar       **contents,
                       gsize        *len)
{
  g_autofree gchar *canon = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (path != NULL);
  g_assert (contents != NULL);
  g_assert (len != NULL);

  *contents = NULL;
  *len = 0;

  /* Canonicalize filename */
  file = g_file_new_for_path (path);
  canon = g_file_get_path (file);

  return g_file_is_native (file) &&
         (g_str_has_prefix (canon, "/proc/") || g_str_has_prefix (canon, "/sys/")) &&
         g_file_get_contents (canon, contents, len, NULL);
}

gboolean
helpers_get_proc_fd (const gchar *path,
                     gint        *out_fd)
{
  g_autofree gchar *canon = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (path != NULL);
  g_assert (out_fd != NULL);

  /* Canonicalize filename */
  file = g_file_new_for_path (path);
  canon = g_file_get_path (file);

  return g_file_is_native (file) &&
         (g_str_has_prefix (canon, "/proc/") || g_str_has_prefix (canon, "/sys/")) &&
         -1 != (*out_fd = open (canon, O_RDONLY | O_CLOEXEC));
}

gboolean
helpers_can_see_pids (void)
{
  g_autofree gchar *contents = NULL;
  gsize len = 0;

  if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS))
    return FALSE;

  if (helpers_get_proc_file ("/proc/mounts", &contents, &len))
    {
      g_auto(GStrv) lines = g_strsplit (contents, "\n", 0);

      for (guint i = 0; lines[i]; i++)
        {
          if (!g_str_has_prefix (lines[i], "proc /proc "))
            continue;

          if (strstr (lines[i], "hidepid=") && !strstr (lines[i], "hidepid=0"))
            return FALSE;

          return TRUE;
        }
    }

  return TRUE;
}

static void
helpers_list_processes_worker (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
  g_autofree gint32 *processes = NULL;
  gsize n_processes;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (helpers_list_processes (&processes, &n_processes))
    {
      GArray *ar;

      ar = g_array_new (FALSE, FALSE, sizeof (gint32));
      g_array_append_vals (ar, processes, n_processes);
      g_task_return_pointer (task,
                             g_steal_pointer (&ar),
                             (GDestroyNotify) g_array_unref);

      return;
    }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Failed to list processes");
}

void
helpers_list_processes_async (GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, helpers_list_processes_async);
  g_task_run_in_thread (task, helpers_list_processes_worker);
}

gboolean
helpers_list_processes_finish (GAsyncResult  *result,
                               gint32       **processes,
                               gsize         *n_processes,
                               GError       **error)
{
  g_autoptr(GArray) ret = NULL;

  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((ret = g_task_propagate_pointer (G_TASK (result), error)))
    {
      if (n_processes)
        *n_processes = ret->len;

      if (processes)
        *processes = (gint32 *)(gpointer)g_array_free (ret, FALSE);

      return TRUE;
    }
  else
    {
      if (processes)
        *processes = NULL;
      if (n_processes)
        *n_processes = 0;
    }

  return FALSE;
}

static gboolean
needs_escape (const gchar *str)
{
  for (; *str; str++)
    {
      if (g_ascii_isspace (*str) || *str == '\'' || *str == '"')
        return TRUE;
    }

  return FALSE;
}

static void
postprocess_cmdline (gchar **str,
                     gsize   len)
{
  g_autoptr(GPtrArray) parts = g_ptr_array_new_with_free_func (g_free);
  g_autofree gchar *instr = NULL;
  const gchar *begin = NULL;

  if (len == 0)
    return;

  instr = *str;

  for (gsize i = 0; i < len; i++)
    {
      if (!begin && instr[i])
        {
          begin = &instr[i];
        }
      else if (begin && instr[i] == '\0')
        {
          if (needs_escape (begin))
            g_ptr_array_add (parts, g_shell_quote (begin));
          else
            g_ptr_array_add (parts, g_strdup (begin));

          begin = NULL;
        }
    }

  /* If the last byte was not \0, as can happen with prctl(), then we need
   * to add it here manually.
   */
  if (begin)
    {
      if (needs_escape (begin))
        g_ptr_array_add (parts, g_shell_quote (begin));
      else
        g_ptr_array_add (parts, g_strdup (begin));
    }

  g_ptr_array_add (parts, NULL);

  *str = g_strjoinv (" ", (gchar **)parts->pdata);
}

static void
postprocess_rstrip (gchar **str,
                    gsize   len)
{
  g_strchomp (*str);
}

static void
add_pid_proc_file_to (gint          pid,
                      const gchar  *name,
                      GVariantDict *dict,
                      void (*postprocess) (gchar **, gsize))
{
  g_autofree gchar *path = NULL;
  g_autofree gchar *contents = NULL;
  gsize len;

  g_assert (pid > -1);
  g_assert (name != NULL);
  g_assert (dict != NULL);

  path = g_strdup_printf ("/proc/%d/%s", pid, name);

  if (g_file_get_contents (path, &contents, &len, NULL))
    {
      if (postprocess)
        postprocess (&contents, len);
      g_variant_dict_insert (dict, name, "s", contents);
    }
}


GVariant *
helpers_get_process_info (const gchar *attributes)
{
  GVariantBuilder builder;
  g_autofree gint *processes = NULL;
  gsize n_processes = 0;
  gboolean want_statm;
  gboolean want_cmdline;
  gboolean want_comm;
  gboolean want_maps;
  gboolean want_mountinfo;
  gboolean want_cgroup;

  if (attributes == NULL)
    attributes = "";

  want_statm = !!strstr (attributes, "statm");
  want_cmdline = !!strstr (attributes, "cmdline");
  want_maps = !!strstr (attributes, "maps");
  want_mountinfo = !!strstr (attributes, "mountinfo");
  want_comm = !!strstr (attributes, "comm");
  want_cgroup = !!strstr (attributes, "cgroup");

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  if (helpers_list_processes (&processes, &n_processes))
    {
      for (guint i = 0; i < n_processes; i++)
        {
          gint pid = processes[i];
          GVariantDict dict;

          g_variant_dict_init (&dict, NULL);
          g_variant_dict_insert (&dict, "pid", "i", pid, NULL);

          if (want_statm)
            add_pid_proc_file_to (pid, "statm", &dict, postprocess_rstrip);

          if (want_cmdline)
            add_pid_proc_file_to (pid, "cmdline", &dict, postprocess_cmdline);

          if (want_comm)
            add_pid_proc_file_to (pid, "comm", &dict, postprocess_rstrip);

          if (want_maps)
            add_pid_proc_file_to (pid, "maps", &dict, postprocess_rstrip);

          if (want_mountinfo)
            add_pid_proc_file_to (pid, "mountinfo", &dict, postprocess_rstrip);

          if (want_cgroup)
            add_pid_proc_file_to (pid, "cgroup", &dict, postprocess_rstrip);

          g_variant_builder_add_value (&builder, g_variant_dict_end (&dict));
        }
    }

  return g_variant_take_ref (g_variant_builder_end (&builder));
}
