/* sysprof-memory-source.c
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-memory-source"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "sysprof-helpers.h"
#include "sysprof-memory-source.h"

#define BUF_SIZE 4096

struct _SysprofMemorySource
{
  GObject parent_instance;

  /* Capture writer to deliver samples */
  SysprofCaptureWriter *writer;

  /* 4k stat buffer for reading proc */
  gchar *stat_buf;

  /* Array of MemStat */
  GArray *mem_stats;

  /* Timeout to perform polling */
  guint timer_source;
};

typedef struct
{
  GPid pid;
  int stat_fd;

  union {
    struct {
      SysprofCaptureCounterValue used;
      gint64 total;
      gint64 avail;
      gint64 free;
    } sys;
    struct {
      SysprofCaptureCounterValue used;
      gint64 size;
      gint64 resident;
      gint64 shared;
      gint64 text;
      gint64 data;
    } proc;
  };

  guint counter_ids[1];
} MemStat;

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofMemorySource, sysprof_memory_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static GHashTable *keys;

static void
mem_stat_open (MemStat *st)
{
  SysprofHelpers *helpers = sysprof_helpers_get_default ();
  g_autoptr(GError) error = NULL;

  g_assert (st != NULL);
  g_assert (st->stat_fd == -1);

  if (st->pid != -1)
    {
      g_autofree gchar *path = g_strdup_printf ("/proc/%d/statm", st->pid);

      if (!sysprof_helpers_get_proc_fd (helpers, path, NULL, &st->stat_fd, &error))
        g_warning ("Failed to access statm for pid %d: %s", st->pid, error->message);
    }
  else
    {
      if (!sysprof_helpers_get_proc_fd (helpers, "/proc/meminfo", NULL, &st->stat_fd, &error))
        g_warning ("Failed to access /proc/statm: %s", error->message);
    }
}

static void
mem_stat_close (MemStat *st)
{
  g_assert (st != NULL);

  if (st->stat_fd != -1)
    {
      close (st->stat_fd);
      st->stat_fd = -1;
    }
}

static void
mem_stat_parse_statm (MemStat *st,
                      gchar   *buf)
{
  g_assert (st != NULL);
  g_assert (buf != NULL);

  sscanf (buf,
          "%"G_GINT64_FORMAT" "
          "%"G_GINT64_FORMAT" "
          "%"G_GINT64_FORMAT" "
          "%"G_GINT64_FORMAT" "
          "%*1c "
          "%"G_GINT64_FORMAT,
          &st->proc.size,
          &st->proc.resident,
          &st->proc.shared,
          &st->proc.text,
          &st->proc.data);

  st->proc.used.vdbl = st->proc.size - st->proc.shared - st->proc.text - st->proc.data;
}

static void
mem_stat_parse_meminfo (MemStat *st,
                        gchar   *buf)
{
  gchar *bufptr = buf;
  gchar *save = NULL;

  g_assert (st != NULL);
  g_assert (buf != NULL);

  for (;;)
    {
      goffset off;
      gchar *key;
      gchar *value;
      gchar *unit;
      gint64 v64;
      gint64 *v64ptr;

      /* Get the data key name */
      if (!(key = strtok_r (bufptr, " \n\t:", &save)))
        break;

      bufptr = NULL;

      /* Offset from self to save value. Stop after getting to
       * last value we care about.
       */
      if (!(off = GPOINTER_TO_UINT (g_hash_table_lookup (keys, key))))
        break;

      /* Get the data value */
      if (!(value = strtok_r (bufptr, " \n\t:", &save)))
        break;

      /* Parse the numeric value of this column */
      v64 = g_ascii_strtoll (value, NULL, 10);
      if ((v64 == G_MININT64 || v64 == G_MAXINT64) && errno == ERANGE)
        break;

      /* Get the data unit */
      unit = strtok_r (bufptr, " \n\t:", &save);

      if (g_strcmp0 (unit, "kB") == 0)
        v64 *= 1024;
      else if (g_strcmp0 (unit, "mB") == 0)
        v64 *= 1024 * 1024;

      v64ptr = (gint64 *)(gpointer)(((gchar *)st) + off);

      *v64ptr = v64;
    }

  /* Create pre-compiled value for used to simplify display */
  st->sys.used.vdbl = (gdouble)st->sys.total - (gdouble)st->sys.avail;
}

static void
mem_stat_poll (MemStat *st,
               gchar   *stat_buf)
{
  gssize r;

  g_assert (st != NULL);
  g_assert (st->stat_fd != -1);

  /* Seek to begin of FD to reset the proc read */
  if ((r = lseek (st->stat_fd, 0, SEEK_SET)) < 0)
    return;

  /* Now read the updated proc buffer */
  if ((r = read (st->stat_fd, stat_buf, BUF_SIZE)) < 0)
    return;

  if (r < BUF_SIZE)
    stat_buf[r] = '\0';
  stat_buf[BUF_SIZE-1] = '\0';

  if (st->pid == -1)
    mem_stat_parse_meminfo (st, stat_buf);
  else
    mem_stat_parse_statm (st, stat_buf);
}

static void
mem_stat_publish (MemStat         *st,
                  SysprofCaptureWriter *writer,
                  gint64           current_time)
{
  g_assert (st != NULL);
  g_assert (writer != NULL);

  sysprof_capture_writer_set_counters (writer,
                                  current_time,
                                  -1,
                                  st->pid,
                                  st->counter_ids,
                                  st->pid == -1 ? &st->sys.used : &st->proc.used,
                                  1);
}

/**
 * sysprof_memory_source_new:
 *
 * Create a new #SysprofMemorySource.
 *
 * Use this source to record basic memory statistics about the system
 * such as Available, Free, and Total Memory.
 *
 * Returns: (transfer full): a newly created #SysprofMemorySource

 * Since: 3.32
 */
SysprofSource *
sysprof_memory_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_MEMORY_SOURCE, NULL);
}

static void
sysprof_memory_source_finalize (GObject *object)
{
  SysprofMemorySource *self = (SysprofMemorySource *)object;

  if (self->timer_source != 0)
    {
      g_source_remove (self->timer_source);
      self->timer_source = 0;
    }

  g_clear_pointer (&self->stat_buf, g_free);
  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->mem_stats, g_array_unref);

  G_OBJECT_CLASS (sysprof_memory_source_parent_class)->finalize (object);
}

static void
sysprof_memory_source_class_init (SysprofMemorySourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_memory_source_finalize;

  keys = g_hash_table_new (g_str_hash, g_str_equal);

#define ADD_OFFSET(n,o) \
  g_hash_table_insert (keys, (gchar *)n, GUINT_TO_POINTER (o))
  ADD_OFFSET ("MemTotal", G_STRUCT_OFFSET (MemStat, sys.total));
  ADD_OFFSET ("MemFree", G_STRUCT_OFFSET (MemStat, sys.free));
  ADD_OFFSET ("MemAvailable", G_STRUCT_OFFSET (MemStat, sys.avail));
#undef ADD_OFFSET
}

static void
sysprof_memory_source_init (SysprofMemorySource *self)
{
  self->stat_buf = g_malloc (BUF_SIZE);
  self->mem_stats = g_array_new (FALSE, FALSE, sizeof (MemStat));
}

static void
sysprof_memory_source_set_writer (SysprofSource        *source,
                             SysprofCaptureWriter *writer)
{
  SysprofMemorySource *self = (SysprofMemorySource *)source;

  g_assert (SYSPROF_IS_SOURCE (self));
  g_assert (writer != NULL);
  g_assert (self->writer == NULL);

  self->writer = sysprof_capture_writer_ref (writer);
}

static void
sysprof_memory_source_prepare (SysprofSource *source)
{
  SysprofMemorySource *self = (SysprofMemorySource *)source;

  g_assert (SYSPROF_IS_MEMORY_SOURCE (self));
  g_assert (self->writer != NULL);

  /* If no pids are registered, setup a system memory stat */
  if (self->mem_stats->len == 0)
    {
      MemStat st = {0};

      st.pid = -1;
      st.stat_fd = -1;

      g_array_append_val (self->mem_stats, st);
    }

  /* Open FDs to all the necessary files in proc. We will re-use
   * them to avoid re-opening files later.
   */
  for (guint i = 0; i < self->mem_stats->len; i++)
    {
      MemStat *st = &g_array_index (self->mem_stats, MemStat, i);
      SysprofCaptureCounter counters[5];
      guint base;

      mem_stat_open (st);

      if (st->pid == -1)
        {
          base = sysprof_capture_writer_request_counter (self->writer, 1);

          g_strlcpy (counters[0].category, "Memory", sizeof counters[0].category);
          g_strlcpy (counters[0].name, "Used", sizeof counters[0].name);
          g_strlcpy (counters[0].description, "Memory used by system", sizeof counters[0].description);

          counters[0].id = st->counter_ids[0] = base;
          counters[0].type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
          counters[0].value.vdbl = 0;

          sysprof_capture_writer_define_counters (self->writer,
                                             SYSPROF_CAPTURE_CURRENT_TIME,
                                             -1,
                                             -1,
                                             counters,
                                             1);
        }
      else
        {
          base = sysprof_capture_writer_request_counter (self->writer, 1);

          g_strlcpy (counters[0].category, "Memory", sizeof counters[0].category);
          g_strlcpy (counters[0].name, "Used", sizeof counters[0].name);
          g_strlcpy (counters[0].description, "Memory used by process", sizeof counters[0].description);

          counters[0].id = st->counter_ids[0] = base;
          counters[0].type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
          counters[0].value.vdbl = 0;

          sysprof_capture_writer_define_counters (self->writer,
                                             SYSPROF_CAPTURE_CURRENT_TIME,
                                             -1,
                                             st->pid,
                                             counters,
                                             1);
        }
    }

  sysprof_source_emit_ready (SYSPROF_SOURCE (self));
}

static gboolean
sysprof_memory_source_timer_cb (SysprofMemorySource *self)
{
  gint64 current_time;

  g_assert (SYSPROF_IS_MEMORY_SOURCE (self));
  g_assert (self->writer != NULL);

  current_time = sysprof_clock_get_current_time ();

  for (guint i = 0; i < self->mem_stats->len; i++)
    {
      MemStat *st = &g_array_index (self->mem_stats, MemStat, i);

      mem_stat_poll (st, self->stat_buf);
      mem_stat_publish (st, self->writer, current_time);
    }

  return G_SOURCE_CONTINUE;
}

static void
sysprof_memory_source_start (SysprofSource *source)
{
  SysprofMemorySource *self = (SysprofMemorySource *)source;

  g_assert (SYSPROF_IS_MEMORY_SOURCE (self));

  /* Poll 4x/sec for memory stats */
  self->timer_source = g_timeout_add_full (G_PRIORITY_HIGH,
                                           1000 / 4,
                                           (GSourceFunc)sysprof_memory_source_timer_cb,
                                           self,
                                           NULL);
}

static void
sysprof_memory_source_stop (SysprofSource *source)
{
  SysprofMemorySource *self = (SysprofMemorySource *)source;

  g_assert (SYSPROF_IS_MEMORY_SOURCE (self));

  if (self->timer_source != 0)
    {
      g_source_remove (self->timer_source);
      self->timer_source = 0;
    }

  for (guint i = 0; i < self->mem_stats->len; i++)
    {
      MemStat *st = &g_array_index (self->mem_stats, MemStat, i);

      mem_stat_close (st);
    }

  sysprof_source_emit_finished (source);
}

static void
sysprof_memory_source_add_pid (SysprofSource *source,
                          GPid      pid)
{
  SysprofMemorySource *self = (SysprofMemorySource *)source;
  MemStat st = {0};

  g_assert (SYSPROF_IS_MEMORY_SOURCE (self));

  st.pid = pid;
  st.stat_fd = -1;

  g_array_append_val (self->mem_stats, st);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_memory_source_set_writer;
  iface->prepare = sysprof_memory_source_prepare;
  iface->start = sysprof_memory_source_start;
  iface->stop = sysprof_memory_source_stop;
  iface->add_pid = sysprof_memory_source_add_pid;
}
