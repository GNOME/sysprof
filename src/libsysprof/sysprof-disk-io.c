/* sysprof-disk-io.c
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

#include "sysprof-disk-io.h"
#include "sysprof-instrument-private.h"
#include "sysprof-perf-event-stream-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#include "line-reader-private.h"

struct _SysprofDiskIo
{
  SysprofInstrument  parent_instance;
  GDBusConnection   *connection;
  SysprofRecording  *recording;
  DexFuture         *cancellable;
  GPtrArray         *streams;
  GHashTable        *device_names;
  GHashTable        *buckets;
  GMutex             buckets_lock;
  gint64             started_at;
  gint64             ended_at;
  gint64             tracepoint_id;
  gint64             dev_offset;
  gint64             bytes_offset;
  gint64             rwbs_offset;
  gint64             rwbs_size;
};

typedef struct
{
  guint32 dev;
  guint32 size;
  guint32 count;
  guint   id;
  guint8  write;
  guint8  define;
} DiskBucket;

typedef struct _SysprofDiskIoClass
{
  SysprofInstrumentClass parent_class;
} SysprofDiskIoClass;

G_DEFINE_FINAL_TYPE (SysprofDiskIo, sysprof_disk_io, SYSPROF_TYPE_INSTRUMENT)

static const char *
sysprof_disk_io_device_name (SysprofDiskIo *self,
                             guint32        dev)
{
  const char *name;
  g_autofree char *path = NULL;
  g_autofree char *contents = NULL;
  char *found;
  char *end;
  guint major = dev >> 20;
  guint minor = dev & 0xfffff;

  if ((name = g_hash_table_lookup (self->device_names, GUINT_TO_POINTER (dev))))
    return name;

  path = g_strdup_printf ("/sys/dev/block/%u:%u/uevent", major, minor);

  if ((contents = dex_await_string (_sysprof_load_file (path), NULL)) &&
      (found = strstr (contents, "DEVNAME=")))
    {
      found += strlen ("DEVNAME=");
      end = strchr (found, '\n');
      name = g_strndup (found, end != NULL ? end - found : strlen (found));
    }
  else
    {
      name = g_strdup_printf ("%u:%u", major, minor);
    }

  g_hash_table_insert (self->device_names, GUINT_TO_POINTER (dev), (gpointer)name);

  return name;
}

static void
sysprof_disk_io_flush (SysprofDiskIo *self)
{
  g_autoptr(GArray) pending = g_array_new (FALSE, FALSE, sizeof (DiskBucket));
  g_autoptr(GArray) ids = g_array_new (FALSE, FALSE, sizeof (guint));
  g_autoptr(GArray) values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));
  SysprofCaptureWriter *writer = self->recording->writer;
  GHashTableIter iter;
  gpointer value;
  gint64 time = SYSPROF_CAPTURE_CURRENT_TIME;

  g_mutex_lock (&self->buckets_lock);
  g_hash_table_iter_init (&iter, self->buckets);

  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      DiskBucket *bucket = value;

      if (bucket->count > 0 || bucket->id != 0)
        {
          DiskBucket captured;
          gboolean new_counter = bucket->id == 0;

          if (new_counter)
            bucket->id = sysprof_capture_writer_request_counter (writer, 1);

          captured = *bucket;
          captured.define = new_counter;
          g_array_append_val (pending, captured);
          bucket->count = 0;
        }
    }

  g_mutex_unlock (&self->buckets_lock);

  for (guint i = 0; i < pending->len; i++)
    {
      DiskBucket *bucket = &g_array_index (pending, DiskBucket, i);
      const char *name = sysprof_disk_io_device_name (self, bucket->dev);
      SysprofCaptureCounterValue counter_value = { .v64 = bucket->count };

      if (bucket->define)
        {
          SysprofCaptureCounter counter = { 0 };

          g_strlcpy (counter.category, "Disk I/O Size", sizeof counter.category);
          g_snprintf (counter.name, sizeof counter.name, "%c %u",
                      bucket->write ? 'W' : 'R', bucket->size);
          g_strlcpy (counter.description, name, sizeof counter.description);
          counter.id = bucket->id;
          counter.type = SYSPROF_CAPTURE_COUNTER_INT64;

          sysprof_capture_writer_define_counters (writer, time, -1, -1, &counter, 1);
        }

      g_array_append_val (ids, bucket->id);
      g_array_append_val (values, counter_value);
    }

  if (ids->len > 0)
    sysprof_capture_writer_set_counters (writer,
                                         time,
                                         -1,
                                         -1,
                                         (const guint *)ids->data,
                                         (const SysprofCaptureCounterValue *)values->data,
                                         ids->len);
}

static void
sysprof_disk_io_event_cb (const SysprofPerfEvent *event,
                          guint                   cpu,
                          gpointer                user_data)
{
  SysprofDiskIo *self = user_data;
  guint32 dev;
  guint32 bytes;
  char rwbs[16] = { 0 };
  guint32 size = 512;
  guint64 key;
  DiskBucket *bucket;
  guint64 raw_end;

  if (event->header.type != PERF_RECORD_SAMPLE ||
      self->started_at == 0 ||
      event->tracepoint.time < self->started_at ||
      (self->ended_at != 0 &&
       event->tracepoint.time > self->ended_at))
    return;

  raw_end = (guint64)self->rwbs_offset + self->rwbs_size;

  if (self->dev_offset < 0 ||
      self->bytes_offset < 0 ||
      raw_end > event->tracepoint.raw_size ||
      self->dev_offset + sizeof dev > event->tracepoint.raw_size ||
      self->bytes_offset + sizeof bytes > event->tracepoint.raw_size)
    return;

  memcpy (&dev, event->tracepoint.raw + self->dev_offset, sizeof dev);
  memcpy (&bytes, event->tracepoint.raw + self->bytes_offset, sizeof bytes);
  memcpy (rwbs,
          event->tracepoint.raw + self->rwbs_offset,
          MIN ((gsize)self->rwbs_size, sizeof rwbs - 1));

  if (bytes == 0 || (rwbs[0] != 'R' && rwbs[0] != 'W'))
    return;

  while (size < bytes && size < (1u << 27))
    size <<= 1;

  key = ((guint64)dev << 32) | ((guint64)size << 1) | (rwbs[0] == 'W');

  g_mutex_lock (&self->buckets_lock);

  if (!(bucket = g_hash_table_lookup (self->buckets, &key)))
    {
      guint64 *stored_key = g_new (guint64, 1);

      *stored_key = key;
      bucket = g_new0 (DiskBucket, 1);
      bucket->dev = dev;
      bucket->size = size;
      bucket->write = rwbs[0] == 'W';

      g_hash_table_insert (self->buckets, stored_key, bucket);
    }

  bucket->count++;

  g_mutex_unlock (&self->buckets_lock);
}

static gboolean
sysprof_disk_io_parse_format (SysprofDiskIo *self,
                              char          *format)
{
  LineReader reader;
  char *line;
  gsize line_len;

  line_reader_init (&reader, format, -1);

  while ((line = line_reader_next (&reader, &line_len)))
    {
      line[line_len] = 0;

      if (g_str_has_prefix (line, "ID: "))
        self->tracepoint_id = g_ascii_strtoll (line + 4, NULL, 10);
      else if (strstr (line, "field:") != NULL)
        {
          g_auto(GStrv) parts = g_strsplit (g_strstrip (line), "\t", 0);
          gint64 offset = -1;
          gint64 size = 0;

          for (guint i = 1; parts[i]; i++)
            {
              if (g_str_has_prefix (parts[i], "offset:"))
                offset = g_ascii_strtoll (parts[i] + 7, NULL, 10);
              else if (g_str_has_prefix (parts[i], "size:"))
                size = g_ascii_strtoll (parts[i] + 5, NULL, 10);
            }

          if (strstr (parts[0], " dev;") && size == 4)
            self->dev_offset = offset;
          else if (strstr (parts[0], " bytes;") && size == 4)
            self->bytes_offset = offset;
          else if (strstr (parts[0], " rwbs[") && size > 0 && size <= 16)
            {
              self->rwbs_offset = offset;
              self->rwbs_size = size;
            }
        }
    }

  return self->tracepoint_id > 0 && self->dev_offset >= 0 &&
         self->bytes_offset >= 0 && self->rwbs_offset >= 0;
}

static DexFuture *
sysprof_disk_io_prepare_fiber (gpointer user_data)
{
  SysprofDiskIo *self = user_data;
  g_autoptr(GBytes) format_bytes = NULL;
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GPtrArray) futures = g_ptr_array_new_with_free_func (dex_unref);
  g_autoptr(GError) error = NULL;
  g_autofree char *format = NULL;

  format_bytes = dex_await_boxed (sysprof_get_proc_file_bytes (self->connection,
                                                               "/sys/kernel/debug/tracing/events/block/block_rq_issue/format"),
                                  &error);

  if (format_bytes == NULL)
    goto unavailable;

  format = g_strndup (g_bytes_get_data (format_bytes, NULL), g_bytes_get_size (format_bytes));

  if (!sysprof_disk_io_parse_format (self, format))
    goto unavailable;

  if (!(cpus = dex_await_boxed (_sysprof_list_online_cpus (), &error)))
    goto unavailable;

  for (guint i = 0; i < cpus->len; i++)
    {
      struct perf_event_attr attr = { 0 };

      guint cpu = g_array_index (cpus, guint, i);

      attr.type = PERF_TYPE_TRACEPOINT;
      attr.config = self->tracepoint_id;
      attr.sample_type = (PERF_SAMPLE_RAW | PERF_SAMPLE_IP | PERF_SAMPLE_TID |
                          PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TIME);
      attr.sample_period = 1;
      attr.size = sizeof attr;
#ifdef HAVE_PERF_CLOCKID
      attr.clockid = sysprof_clock;
      attr.use_clockid = 1;
#endif
      g_ptr_array_add (futures,
                       sysprof_perf_event_stream_new (self->connection,
                                                      &attr,
                                                      16,
                                                      cpu,
                                                      -1,
                                                      0,
                                                      sysprof_disk_io_event_cb,
                                                      self,
                                                      NULL));
    }

  if (!dex_await (dex_future_allv ((DexFuture **)futures->pdata, futures->len), &error))
    goto unavailable;

  for (guint i = 0; i < futures->len; i++)
    {
      g_autoptr(SysprofPerfEventStream) stream = dex_await_object (dex_ref (g_ptr_array_index (futures, i)), NULL);

      if (stream != NULL && sysprof_perf_event_stream_enable (stream, NULL))
        g_ptr_array_add (self->streams, g_object_ref (stream));
    }

  return dex_future_new_true ();

unavailable:
  _sysprof_recording_diagnostic (self->recording,
                                 "Disk I/O",
                                 "Block request tracing is unavailable: %s",
                                 error != NULL ? error->message : "Unknown tracepoint format");

  return dex_future_new_true ();
}

static DexFuture *
sysprof_disk_io_prepare (SysprofInstrument *instrument,
                         SysprofRecording  *recording)
{
  SysprofDiskIo *self = (SysprofDiskIo *)instrument;

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL,
                              0,
                              sysprof_disk_io_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
sysprof_disk_io_record_fiber (gpointer user_data)
{
  SysprofDiskIo *self = user_data;

  self->started_at = SYSPROF_CAPTURE_CURRENT_TIME;

  for (;;)
    {
      g_autoptr(GError) error = NULL;

      dex_await (dex_future_first (dex_ref (self->cancellable),
                                   dex_timeout_new_msec (500),
                                   NULL),
                 &error);

      if (!g_error_matches (error, DEX_ERROR, DEX_ERROR_TIMED_OUT))
        break;

      sysprof_disk_io_flush (self);
    }

  self->ended_at = SYSPROF_CAPTURE_CURRENT_TIME;

  for (guint i = 0; i < self->streams->len; i++)
    sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);

  g_ptr_array_set_size (self->streams, 0);

  sysprof_disk_io_flush (self);

  return dex_future_new_true ();
}

static DexFuture *
sysprof_disk_io_record (SysprofInstrument *instrument,
                        SysprofRecording  *recording,
                        GCancellable      *cancellable)
{
  SysprofDiskIo *self = (SysprofDiskIo *)instrument;

  self->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              sysprof_disk_io_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
sysprof_disk_io_set_connection (SysprofInstrument *instrument,
                                GDBusConnection   *connection)
{
  SysprofDiskIo *self = (SysprofDiskIo *)instrument;

  g_set_object (&self->connection, connection);
}

static void
sysprof_disk_io_dispose (GObject *object)
{
  SysprofDiskIo *self = (SysprofDiskIo *)object;

  for (guint i = 0; i < self->streams->len; i++)
    sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);

  g_clear_pointer (&self->streams, g_ptr_array_unref);
  g_clear_pointer (&self->device_names, g_hash_table_unref);
  g_clear_pointer (&self->buckets, g_hash_table_unref);
  g_mutex_clear (&self->buckets_lock);
  g_clear_object (&self->connection);
  g_clear_object (&self->recording);
  dex_clear (&self->cancellable);

  G_OBJECT_CLASS (sysprof_disk_io_parent_class)->dispose (object);
}

static void
sysprof_disk_io_class_init (SysprofDiskIoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_disk_io_dispose;

  instrument_class->prepare = sysprof_disk_io_prepare;
  instrument_class->record = sysprof_disk_io_record;
  instrument_class->set_connection = sysprof_disk_io_set_connection;
}

static void
sysprof_disk_io_init (SysprofDiskIo *self)
{
  self->dev_offset = -1;
  self->bytes_offset = -1;
  self->rwbs_offset = -1;
  self->streams = g_ptr_array_new_with_free_func (g_object_unref);
  self->device_names = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
  self->buckets = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
  g_mutex_init (&self->buckets_lock);
}

/**
 * sysprof_disk_io_new:
 *
 * Creates an instrument that records issued block request size buckets.
 * The instrument reports a diagnostic and continues when block tracepoints
 * are unavailable, so ordinary disk counters can still be recorded.
 *
 * Returns: (transfer full): a new #SysprofInstrument
 */
SysprofInstrument *
sysprof_disk_io_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISK_IO, NULL);
}
