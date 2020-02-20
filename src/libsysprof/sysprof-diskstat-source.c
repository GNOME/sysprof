/* sysprof-diskstat-source.c
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

#define G_LOG_DOMAIN "sysprof-diskstat-source"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sysprof-backport-autocleanups.h"
#include "sysprof-line-reader.h"
#include "sysprof-diskstat-source.h"
#include "sysprof-helpers.h"

#define ADD_FROM_CHAR(v, c) (((v)*10L)+((c)-'0'))

struct _SysprofDiskstatSource
{
  GObject               parent_instance;

  SysprofCaptureWriter *writer;
  GArray               *diskstats;

  /* FD for /proc/net/dev contents */
  gint                  diskstat_fd;

  /* GSource ID for polling */
  guint                 poll_source;

  guint                 ignore_next_poll : 1;
};

typedef struct
{
  /* Counter IDs */
  guint reads_total_id;
  guint writes_total_id;

  gchar  device[32];
  gint64 reads_total;
  gint64 reads_merged;
  gint64 reads_sectors;
  gint64 reads_msec;
  gint64 writes_total;
  gint64 writes_merged;
  gint64 writes_sectors;
  gint64 writes_msec;
  gint64 iops_active;
  gint64 iops_msec;
  gint64 iops_msec_weighted;
} Diskstat;

enum {
  /* -2 */ COLUMN_MAJOR,
  /* -1 */ COLUMN_MINOR,
  /* 0 */  COLUMN_NAME,
  /* 1 */  COLUMN_READS_TOTAL,
  /* 2 */  COLUMN_READS_MERGED,
  /* 3 */  COLUMN_READS_SECTORS,
  /* 4 */  COLUMN_READS_MSEC,
  /* 5 */  COLUMN_WRITES_TOTAL,
  /* 6 */  COLUMN_WRITES_MERGED,
  /* 7 */  COLUMN_WRITES_SECTORS,
  /* 8 */  COLUMN_WRITES_MSEC,
  /* 9 */  COLUMN_IOPS_ACTIVE,
  /* 10 */ COLUMN_IOPS_MSEC,
  /* 11 */ COLUMN_IOPS_MSEC_WEIGHTED,
};

static void source_iface_init (SysprofSourceInterface *);

G_DEFINE_TYPE_WITH_CODE (SysprofDiskstatSource, sysprof_diskstat_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static Diskstat *
find_device_by_name (SysprofDiskstatSource *self,
                     const gchar           *name)
{
  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));
  g_assert (self->writer != NULL);
  g_assert (name != NULL);

  for (guint i = 0; i < self->diskstats->len; i++)
    {
      Diskstat *diskstat = &g_array_index (self->diskstats, Diskstat, i);

      if (strcmp (name, diskstat->device) == 0)
        return diskstat;
    }

  return NULL;
}

static Diskstat *
register_counters_by_name (SysprofDiskstatSource *self,
                           const gchar           *name)
{
  SysprofCaptureCounter ctr[2] = {{{0}}};
  Diskstat ds = {0};

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));
  g_assert (name != NULL);
  g_assert (self->writer != NULL);

  ds.reads_total_id = sysprof_capture_writer_request_counter (self->writer, 1);
  ds.writes_total_id = sysprof_capture_writer_request_counter (self->writer, 1);

  g_strlcpy (ds.device, name, sizeof ds.device);

  g_strlcpy (ctr[0].category, "Disk", sizeof ctr[0].category);
  g_snprintf (ctr[0].name, sizeof ctr[0].name, "Total Reads (%s)", name);
  g_strlcpy (ctr[0].description, name, sizeof ctr[0].description);
  ctr[0].id = ds.reads_total_id;
  ctr[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[0].value.v64 = 0;

  g_strlcpy (ctr[1].category, "Disk", sizeof ctr[1].category);
  g_snprintf (ctr[1].name, sizeof ctr[1].name, "Total Writes (%s)", name);
  g_strlcpy (ctr[1].description, name, sizeof ctr[1].description);
  ctr[1].id = ds.writes_total_id;
  ctr[1].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[1].value.v64 = 1;

  sysprof_capture_writer_define_counters (self->writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          ctr, G_N_ELEMENTS (ctr));

  g_array_append_val (self->diskstats, ds);
  return &g_array_index (self->diskstats, Diskstat, self->diskstats->len - 1);
}

static gboolean
sysprof_diskstat_source_get_is_ready (SysprofSource *source)
{
  return TRUE;
}

static gboolean
sysprof_diskstat_source_poll_cb (gpointer data)
{
  g_autoptr(SysprofLineReader) reader = NULL;
  SysprofDiskstatSource *self = data;
  g_autoptr(GArray) counters = NULL;
  g_autoptr(GArray) values = NULL;
  gchar buf[4096*4];
  Diskstat *found;
  gint64 combined_reads_total = 0;
  gint64 combined_writes_total = 0;
  gssize len;
  gsize line_len;
  gchar *line;

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));

  if (self->diskstat_fd == -1)
    {
      self->poll_source = 0;
      return G_SOURCE_REMOVE;
    }

  /* Seek to 0 forces reload of data */
  lseek (self->diskstat_fd, 0, SEEK_SET);

  len = read (self->diskstat_fd, buf, sizeof buf - 1);

  /* Bail for now unless we read enough data */
  if (len > 0)
    buf[len] = 0;
  else
    return G_SOURCE_CONTINUE;

  counters = g_array_new (FALSE, FALSE, sizeof (guint));
  values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));
  reader = sysprof_line_reader_new (buf, len);

#if 0
Entries looks like this...
------------------------------------------------------------------------------------------------------------------------------
 259       0 nvme0n1 455442 37 15274065 82579 714090 524038 44971827 1078532 0 244176 957543 0 0 0 0
 259       1 nvme0n1p1 403 0 10411 102 61 39 33171 72 0 61 53 0 0 0 0
 259       2 nvme0n1p2 138 0 9594 29 7 1 64 20 0 49 24 0 0 0 0
 259       3 nvme0n1p3 454820 37 15249700 82432 688488 523998 44938592 1038914 0 237085 924545 0 0 0 0
 253       0 dm-0 454789 0 15246924 107870 1238001 0 44938592 7873687 0 248354 7981557 0 0 0 0
 253       1 dm-1 92473 0 4824170 27656 179001 0 5876760 1378161 0 45291 1405817 0 0 0 0
 253       2 dm-2 206 0 5552 100 240 0 1920 297 0 219 397 0 0 0 0
 253       3 dm-3 362064 0 10414850 80861 1058760 0 39245920 6496983 0 206426 6577844 0 0 0 0
------------------------------------------------------------------------------------------------------------------------------
#endif

  while ((line = (gchar *)sysprof_line_reader_next (reader, &line_len)))
    {
      Diskstat ds = {0};
      gint64 dummy = 0;
      gint column = COLUMN_MAJOR;

      line[line_len] = 0;

      /* Skip past initial space */
      while (g_ascii_isspace (*line))
        line++;

      for (const gchar *ptr = line; *ptr; ptr++)
        {
          gchar ch;

          /* Skip past space and advance to next column */
          if (g_ascii_isspace (*ptr))
            {
              while (g_ascii_isspace (*ptr))
                ptr++;
              column++;
            }

          ch = *ptr;

          switch (column)
            {
            case COLUMN_MAJOR:
            case COLUMN_MINOR:
            default:
              dummy = ADD_FROM_CHAR (dummy, ch);
              break;

            case COLUMN_NAME:
              {
                guint j;

                for (j = 0; j < sizeof ds.device && ds.device[j] != 0; j++) { /* Do Nothing */ }
                if (j < sizeof ds.device)
                  ds.device[j] = ch;
                ds.device[sizeof ds.device - 1] = 0;

                break;
              }

            case COLUMN_READS_TOTAL:
              ds.reads_total = ADD_FROM_CHAR (ds.reads_total, ch);
              break;

            case COLUMN_READS_MERGED:
              ds.reads_merged = ADD_FROM_CHAR (ds.reads_merged, ch);
              break;

            case COLUMN_READS_SECTORS:
              ds.reads_sectors = ADD_FROM_CHAR (ds.reads_sectors, ch);
              break;

            case COLUMN_READS_MSEC:
              ds.reads_msec = ADD_FROM_CHAR (ds.reads_msec, ch);
              break;

            case COLUMN_WRITES_TOTAL:
              ds.writes_total = ADD_FROM_CHAR (ds.writes_total, ch);
              break;

            case COLUMN_WRITES_MERGED:
              ds.writes_merged = ADD_FROM_CHAR (ds.writes_merged, ch);
              break;

            case COLUMN_WRITES_SECTORS:
              ds.writes_sectors = ADD_FROM_CHAR (ds.writes_sectors, ch);
              break;

            case COLUMN_WRITES_MSEC:
              ds.writes_msec = ADD_FROM_CHAR (ds.writes_msec, ch);
              break;

            case COLUMN_IOPS_ACTIVE:
              ds.iops_active = ADD_FROM_CHAR (ds.iops_active, ch);
              break;

            case COLUMN_IOPS_MSEC:
              ds.iops_msec = ADD_FROM_CHAR (ds.iops_msec, ch);
              break;

            case COLUMN_IOPS_MSEC_WEIGHTED:
              ds.iops_msec_weighted = ADD_FROM_CHAR (ds.iops_msec_weighted, ch);
              break;
            }
        }

      g_strstrip (ds.device);

      if (ds.device[0])
        {
          gint64 reads_total;
          gint64 writes_total;

          if (!(found = find_device_by_name (self, ds.device)))
            found = register_counters_by_name (self, ds.device);

          /* Stash new value, based on diff from previous */
          reads_total = ds.reads_total - found->reads_total;
          writes_total = ds.writes_total - found->writes_total;

          g_array_append_val (counters, found->reads_total_id);
          g_array_append_val (values, reads_total);

          g_array_append_val (counters, found->writes_total_id);
          g_array_append_val (values, writes_total);

          /* Update combined values */
          combined_reads_total += reads_total;
          combined_writes_total += writes_total;

          /* Save current value for diff on next poll */
          found->reads_total = ds.reads_total;
          found->writes_total = ds.writes_total;
        }
    }

  if (!(found = find_device_by_name (self, "Combined")))
    found = register_counters_by_name (self, "Combined");

  g_array_append_val (counters, found->reads_total_id);
  g_array_append_val (values, combined_reads_total);

  g_array_append_val (counters, found->writes_total_id);
  g_array_append_val (values, combined_writes_total);

  if (self->ignore_next_poll)
    self->ignore_next_poll = FALSE;
  else
    sysprof_capture_writer_set_counters (self->writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         -1,
                                         -1,
                                         (const guint *)(gpointer)counters->data,
                                         (const SysprofCaptureCounterValue *)(gpointer)values->data,
                                         counters->len);

  return G_SOURCE_CONTINUE;
}

static void
sysprof_diskstat_source_prepare (SysprofSource *source)
{
  SysprofDiskstatSource *self = (SysprofDiskstatSource *)source;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));

  self->diskstat_fd = g_open ("/proc/diskstats", O_RDONLY, 0);

  if (self->diskstat_fd == -1)
    {
      int errsv = errno;
      error = g_error_new (G_FILE_ERROR,
                           g_file_error_from_errno (errsv),
                           "%s",
                           g_strerror (errsv));
      sysprof_source_emit_failed (source, error);
      return;
    }

  self->ignore_next_poll = TRUE;
  sysprof_diskstat_source_poll_cb (self);

  sysprof_source_emit_ready (source);
}

static void
sysprof_diskstat_source_start (SysprofSource *source)
{
  SysprofDiskstatSource *self = (SysprofDiskstatSource *)source;

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));

  self->poll_source = g_timeout_add (200, sysprof_diskstat_source_poll_cb, self);

  /* Poll immediately */
  sysprof_diskstat_source_poll_cb (self);
}

static void
sysprof_diskstat_source_stop (SysprofSource *source)
{
  SysprofDiskstatSource *self = (SysprofDiskstatSource *)source;

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));

  /* Poll one last time */
  sysprof_diskstat_source_poll_cb (self);

  g_clear_handle_id (&self->poll_source, g_source_remove);

  sysprof_source_emit_finished (source);
}

static void
sysprof_diskstat_source_set_writer (SysprofSource        *source,
                                  SysprofCaptureWriter *writer)
{
  SysprofDiskstatSource *self = (SysprofDiskstatSource *)source;

  g_assert (SYSPROF_IS_DISKSTAT_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  self->writer = sysprof_capture_writer_ref (writer);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->get_is_ready = sysprof_diskstat_source_get_is_ready;
  iface->prepare = sysprof_diskstat_source_prepare;
  iface->set_writer = sysprof_diskstat_source_set_writer;
  iface->start = sysprof_diskstat_source_start;
  iface->stop = sysprof_diskstat_source_stop;
}

static void
sysprof_diskstat_source_finalize (GObject *object)
{
  SysprofDiskstatSource *self = (SysprofDiskstatSource *)object;

  g_clear_pointer (&self->diskstats, g_array_unref);
  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  if (self->diskstat_fd != -1)
    {
      close (self->diskstat_fd);
      self->diskstat_fd = -1;
    }

  G_OBJECT_CLASS (sysprof_diskstat_source_parent_class)->finalize (object);
}

static void
sysprof_diskstat_source_class_init (SysprofDiskstatSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_diskstat_source_finalize;
}

static void
sysprof_diskstat_source_init (SysprofDiskstatSource *self)
{
  self->diskstats = g_array_new (FALSE, FALSE, sizeof (Diskstat));
}

SysprofSource *
sysprof_diskstat_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISKSTAT_SOURCE, NULL);
}
