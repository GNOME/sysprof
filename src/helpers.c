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
                         gulong    flags,
                         gint     *out_fd)
{
#ifndef __linux__
  *out_fd = -1;
  return FALSE;
#else
  struct perf_event_attr attr = {0};
  GVariantIter iter;
  GVariant *value;
  gchar *key;
  gint32 disabled = 0;
  gint32 wakeup_events = 149;
  gint32 type = 0;
  guint64 sample_period = 0;
  guint64 sample_type = 0;
  guint64 config = 0;
  gint clockid = CLOCK_MONOTONIC;
  gint comm = 0;
  gint mmap_ = 0;
  gint task = 0;
  gint exclude_idle = 0;
  gint use_clockid = 0;
  gint sample_id_all = 0;

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
  attr.sample_id_all = sample_id_all;
  attr.sample_period = sample_period;
  attr.sample_type = sample_type;
  attr.task = !!task;
  attr.type = type;
  attr.wakeup_events = wakeup_events;

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
         g_str_has_prefix (canon, "/proc/") &&
         g_file_get_contents (canon, contents, len, NULL);
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
