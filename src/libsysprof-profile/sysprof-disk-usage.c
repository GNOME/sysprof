/* sysprof-disk-usage.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <glib/gstdio.h>

#include "sysprof-disk-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

#include "line-reader-private.h"

#define ADD_FROM_CHAR(v, c) (((v)*10L)+((c)-'0'))

struct _SysprofDiskUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofDiskUsageClass
{
  SysprofInstrumentClass parent_class;
};

typedef struct _DiskUsage
{
  /* Counter IDs */
  guint reads_total_id;
  guint writes_total_id;

  /* Index where values are stored */
  guint values_at;

  char  device[32];

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
} DiskUsage;

typedef enum _Column
{
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
} Column;

G_DEFINE_FINAL_TYPE (SysprofDiskUsage, sysprof_disk_usage, SYSPROF_TYPE_INSTRUMENT)

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
  GArray           *devices;
  GArray           *ids;
  GArray           *values;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  g_clear_pointer (&record->devices, g_array_unref);
  g_clear_pointer (&record->ids, g_array_unref);
  g_clear_pointer (&record->values, g_array_unref);
  dex_clear (&record->cancellable);
  g_free (record);
}

static DiskUsage *
register_counters_by_name (Record     *record,
                           const char *name)
{
  static const SysprofCaptureCounterValue zeroval = {0};
  SysprofCaptureCounter ctr[2] = {0};
  SysprofCaptureWriter *writer;
  DiskUsage ds = {0};

  g_assert (record != NULL);
  g_assert (name != NULL);

  writer = _sysprof_recording_writer (record->recording);

  ds.values_at = record->ids->len;
  ds.reads_total_id = sysprof_capture_writer_request_counter (writer, 1);
  ds.writes_total_id = sysprof_capture_writer_request_counter (writer, 1);

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
  ctr[1].value.v64 = 0;

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          ctr,
                                          G_N_ELEMENTS (ctr));

  g_array_append_val (record->devices, ds);
  g_array_append_val (record->ids, ds.reads_total_id);
  g_array_append_val (record->ids, ds.writes_total_id);
  g_array_append_val (record->values, zeroval);
  g_array_append_val (record->values, zeroval);

  return &g_array_index (record->devices, DiskUsage, record->devices->len-1);
}

static DiskUsage *
find_device_by_name (Record     *record,
                     const char *name)
{
  g_assert (record != NULL);
  g_assert (record->devices != NULL);
  g_assert (name != NULL);

  for (guint i = 0; i < record->devices->len; i++)
    {
      DiskUsage *ds = &g_array_index (record->devices, DiskUsage, i);

      if (strcmp (name, ds->device) == 0)
        return ds;
    }

  return NULL;
}

static DexFuture *
sysprof_disk_usage_record_fiber (gpointer user_data)
{
  g_autoptr(GByteArray) buf = NULL;
  Record *record = user_data;
  SysprofCaptureWriter *writer;
  g_autofd int stat_fd = -1;
  LineReader reader;
  DiskUsage *combined;
  gint64 combined_reads_total = 0;
  gint64 combined_writes_total = 0;
  gboolean skip_publish = TRUE;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_CANCELLABLE (record->cancellable));

  buf = g_byte_array_new ();
  g_byte_array_set_size (buf, 4096*4);

  if (-1 == (stat_fd = open ("/proc/diskstats", O_RDONLY|O_CLOEXEC)))
    return dex_future_new_for_errno (errno);

  writer = _sysprof_recording_writer (record->recording);

  register_counters_by_name (record, "Combined");

  for (;;)
    {
      g_autoptr(DexFuture) read_future = NULL;
      gssize n_read;
      char *line;
      gsize line_len;

      /* Read a new copy of our diskstats or bail from our
       * recording loop. If cancellation future rejects, then
       * we also break out of our recording loop.
       */
      read_future = dex_aio_read (NULL, stat_fd, buf->data, buf->len-1, 0);
      if (!dex_await (dex_future_first (dex_ref (record->cancellable),
                                        dex_ref (read_future),
                                        NULL),
                      NULL))
        break;

      n_read = dex_await_int64 (dex_ref (read_future), NULL);
      if (n_read < 0)
        break;

      line_reader_init (&reader, (char *)buf->data, n_read);
      while ((line = line_reader_next (&reader, &line_len)))
        {
          DiskUsage ds = {0};
          DiskUsage *found;
          gint64 dummy = 0;
          int column = COLUMN_MAJOR;

          line[line_len] = 0;

          /* Skip past initial space */
          while (g_ascii_isspace (*line))
            line++;

          for (const char *ptr = line; *ptr; ptr++)
            {
              char ch;

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
              guint p;
              gint64 reads_total;
              gint64 writes_total;

              if (!(found = find_device_by_name (record, ds.device)))
                found = register_counters_by_name (record, ds.device);

              /* Calculate new value, based on diff from previous */
              reads_total = ds.reads_total - found->reads_total;
              writes_total = ds.writes_total - found->writes_total;

              /* Update value for publishing */
              p = found->values_at;
              g_array_index (record->values, SysprofCaptureCounterValue, p).v64 = reads_total;
              g_array_index (record->values, SysprofCaptureCounterValue, p+1).v64 = writes_total;

              /* Update combined values */
              combined_reads_total += reads_total;
              combined_writes_total += writes_total;

              /* Save current value for diff on next poll */
              found->reads_total = ds.reads_total;
              found->writes_total = ds.writes_total;
            }
        }

      if ((combined = find_device_by_name (record, "Combined")))
        {
          /* TODO: It would be nice to not double count disk ops multiple
           * times based on the parition, etc.
           *
           * For example:  nvme0n1 nvme0n1p1 nvme0n1p2 nvme0n1p3 may get
           * accounted multiple times even though they are all nvme0n1.
           *
           * The other option, is to just not do "Combined" counters.
           */

          g_array_index (record->values,
                         SysprofCaptureCounterValue,
                         combined->values_at).v64
            = combined_reads_total - combined->reads_total;
          g_array_index (record->values,
                         SysprofCaptureCounterValue,
                         combined->values_at+1).v64
            = combined_writes_total - combined->writes_total;

          combined->reads_total = combined_reads_total;
          combined->writes_total = combined_writes_total;
        }

      g_assert (record->ids->len == record->values->len);

      if (skip_publish)
        skip_publish = FALSE;
      else
        sysprof_capture_writer_set_counters (writer,
                                             SYSPROF_CAPTURE_CURRENT_TIME,
                                             -1,
                                             -1,
                                             (const guint *)(gpointer)record->ids->data,
                                             (const SysprofCaptureCounterValue *)(gpointer)record->values->data,
                                             record->ids->len);

      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 2),
                                   NULL),
                 NULL);
      if (dex_future_get_status (record->cancellable) != DEX_FUTURE_STATUS_PENDING)
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_disk_usage_record (SysprofInstrument *instrument,
                           SysprofRecording  *recording,
                           GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);
  record->devices = g_array_new (FALSE, FALSE, sizeof (DiskUsage));
  record->ids = g_array_new (FALSE, FALSE, sizeof (guint));
  record->values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_disk_usage_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_disk_usage_class_init (SysprofDiskUsageClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_disk_usage_record;
}

static void
sysprof_disk_usage_init (SysprofDiskUsage *self)
{
}

SysprofInstrument *
sysprof_disk_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISK_USAGE, NULL);
}
