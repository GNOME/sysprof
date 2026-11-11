/* sysprof-gpu-jobs.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include "sysprof-gpu-jobs.h"
#include "sysprof-gpu-jobs-private.h"
#include "sysprof-instrument-private.h"
#include "sysprof-perf-event-stream-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

typedef struct
{
  guint64 context;
  guint64 seqno;
} GpuKey;

typedef struct
{
  SysprofGpuJobStages  stages;
  char                *dev;
  char                *ring;
} GpuJob;

struct _SysprofGpuJobs
{
  SysprofInstrument  parent_instance;

  SysprofRecording  *recording;
  DexFuture         *cancellable;
  GPtrArray         *streams;
  GHashTable        *jobs;
  SysprofGpuFormat   formats[3];
  gint64             started_at;
  gint64             ended_at;
  guint              bad_samples;
  guint              dropped_jobs;
  guint              valid_events;
  gboolean           tracepoints_enabled;
};

struct _SysprofGpuJobsClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofGpuJobs, sysprof_gpu_jobs, SYSPROF_TYPE_INSTRUMENT)

static guint
gpu_key_hash (gconstpointer data)
{
  const GpuKey *key = data;

  return (guint)(key->context ^ (key->context >> 32) ^ key->seqno ^ (key->seqno >> 32));
}

static gboolean
gpu_key_equal (gconstpointer a,
               gconstpointer b)
{
  const GpuKey *left = a;
  const GpuKey *right = b;

  return left->context == right->context && left->seqno == right->seqno;
}

static void
gpu_job_free (gpointer data)
{
  GpuJob *job = data;

  g_free (job->dev);
  g_free (job->ring);
  g_free (job);
}

static void
gpu_parse_field (const char      *line,
                 const char      *name,
                 SysprofGpuField *field)
{
  g_autofree char *needle = g_strdup_printf (" %s;", name);
  const char *offset;
  const char *size;

  if (strstr (line, needle) == NULL)
    return;

  offset = strstr (line, "offset:");
  size = strstr (line, "size:");

  if (offset != NULL && size != NULL)
    {
      field->offset = (guint)g_ascii_strtoull (offset + 7, NULL, 10);
      field->size = (guint)g_ascii_strtoull (size + 5, NULL, 10);
    }
}

gboolean
_sysprof_gpu_jobs_parse_format (const char       *text,
                                SysprofGpuFormat *format,
                                gboolean          require_names)
{
  g_auto(GStrv) lines = NULL;

  g_return_val_if_fail (text != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  lines = g_strsplit (text, "\n", 0);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      const char *line = lines[i];

      if (g_str_has_prefix (line, "ID: "))
        {
          format->id = (guint)g_ascii_strtoull (line + 4, NULL, 10);
        }
      else if (strstr (line, "field:") != NULL)
        {
          gpu_parse_field (line, "fence_context", &format->context);
          gpu_parse_field (line, "fence_seqno", &format->seqno);
          gpu_parse_field (line, "dev", &format->dev);
          gpu_parse_field (line, "name", &format->ring);
          gpu_parse_field (line, "ring", &format->ring);
        }
    }

  return format->id > 0 && format->context.size == 8 && format->seqno.size == 8 &&
         (!require_names || (format->dev.size == 4 && format->ring.size == 4));
}

static gboolean
gpu_field_fits (SysprofGpuField field,
                gsize           length)
{
  return field.size > 0 && field.offset <= length && field.size <= length - field.offset;
}

static char *
gpu_read_string (const guint8    *raw,
                 gsize            length,
                 SysprofGpuField  field)
{
  guint32 location;
  guint offset;
  guint size;

  if (!gpu_field_fits (field, length) || field.size != 4)
    return NULL;

  memcpy (&location, raw + field.offset, 4);

  offset = location & 0xffff;
  size = location >> 16;

  if (size == 0 ||
      offset > length ||
      size > length - offset ||
      memchr (raw + offset, 0, size) == NULL)
    return NULL;

  return g_strndup ((const char *)raw + offset, size - 1);
}

gboolean
_sysprof_gpu_jobs_read_sample (const SysprofGpuFormat  *format,
                               const guint8            *raw,
                               gsize                    length,
                               guint64                 *context,
                               guint64                 *seqno,
                               char                   **dev,
                               char                   **ring)
{
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (raw != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (seqno != NULL, FALSE);

  if (!gpu_field_fits (format->context, length) ||
      !gpu_field_fits (format->seqno, length))
    return FALSE;

  memcpy (context, raw + format->context.offset, 8);
  memcpy (seqno, raw + format->seqno.offset, 8);

  if (format->dev.size != 0)
    {
      char *parsed_dev;
      char *parsed_ring;

      if (dev == NULL || ring == NULL)
        return FALSE;

      parsed_dev = gpu_read_string (raw, length, format->dev);
      parsed_ring = gpu_read_string (raw, length, format->ring);

      if (parsed_dev == NULL || parsed_ring == NULL)
        {
          g_free (parsed_dev);
          g_free (parsed_ring);
          return FALSE;
        }

      *dev = parsed_dev;
      *ring = parsed_ring;
    }

  return TRUE;
}

gboolean
_sysprof_gpu_jobs_sample_payload (const SysprofPerfEvent  *event,
                                  const guint8           **raw,
                                  gsize                   *length)
{
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (raw != NULL, FALSE);
  g_return_val_if_fail (length != NULL, FALSE);

  if (event->header.type != PERF_RECORD_SAMPLE ||
      event->header.size < offsetof (SysprofPerfEventTracepoint, raw) ||
      event->tracepoint.raw_size > event->header.size - offsetof (SysprofPerfEventTracepoint, raw))
    return FALSE;

  *raw = event->tracepoint.raw;
  *length = event->tracepoint.raw_size;

  return TRUE;
}

guint
_sysprof_gpu_job_add_stage (SysprofGpuJobStages *stages,
                            guint                stage,
                            gint64               time)
{
  guint ready = 0;

  g_return_val_if_fail (stages != NULL, 0);
  g_return_val_if_fail (stage < 3, 0);

  if (time <= 0 || stages->times[stage] != 0)
    return 0;

  stages->times[stage] = time;

  for (guint i = 0; i < 2; i++)
    {
      if (stages->times[i] > 0 && stages->times[i + 1] > 0 &&
          !(stages->emitted & (1u << i)))
        {
          stages->emitted |= 1u << i;

          if (stages->times[i + 1] > stages->times[i])
            ready |= 1u << i;
        }
    }

  return ready;
}

gboolean
_sysprof_gpu_job_span_in_capture (gint64 begin,
                                  gint64 end,
                                  gint64 started_at,
                                  gint64 ended_at)
{
  return begin > 0 && end > begin && begin >= started_at &&
         (ended_at == 0 || end <= ended_at);
}

static void
gpu_emit_span (SysprofGpuJobs *self,
               GpuJob         *job,
               const GpuKey   *key,
               guint           stage,
               guint           cpu)
{
  g_autofree char *details = NULL;
  g_autofree char *name = NULL;
  g_autofree char *lane = NULL;
  gint64 begin = job->stages.times[stage];
  gint64 end = job->stages.times[stage + 1];

  if (!_sysprof_gpu_job_span_in_capture (begin, end, self->started_at, self->ended_at))
    return;

  details = g_strdup_printf ("%s / %s · fence %" G_GUINT64_FORMAT ":%" G_GUINT64_FORMAT,
                             job->dev ? job->dev : "Unknown device",
                             job->ring ? job->ring : "Unknown ring",
                             key->context,
                             key->seqno);
  lane = g_strdup_printf ("%s/%s",
                          job->dev ? job->dev : "?",
                          job->ring ? job->ring : "?");
  name = g_strdup_printf ("%s %08x %s",
                          stage == 0 ? "Queue wait" : "In flight",
                          g_str_hash (lane),
                          lane);

  if (strlen (name) >= sizeof ((SysprofCaptureMark *)0)->name)
    g_set_str_take (&name, g_strndup (name, sizeof ((SysprofCaptureMark *)0)->name - 1));

  sysprof_capture_writer_add_mark (self->recording->writer,
                                   begin,
                                   cpu,
                                   -1,
                                   end - begin,
                                   "GPU Jobs",
                                   name,
                                   details);
}

static void
gpu_event (const SysprofPerfEvent *event,
           guint                   cpu,
           SysprofGpuJobs         *self,
           guint                   stage)
{
  const SysprofGpuFormat *format = &self->formats[stage];
  const guint8 *raw;
  gsize length;
  GpuKey key;
  GpuJob *job;
  char *dev = NULL;
  char *ring = NULL;
  guint ready;

  if (event->header.type == PERF_RECORD_LOST)
    {
      if (event->header.size >= sizeof event->lost)
        _sysprof_recording_diagnostic (self->recording,
                                       "GPU Jobs",
                                       "Lost %" G_GUINT64_FORMAT " perf records on CPU %u",
                                       event->lost.lost,
                                       cpu);
      g_hash_table_remove_all (self->jobs);
      return;
    }

  if (event->header.type != PERF_RECORD_SAMPLE)
    return;

  if (!_sysprof_gpu_jobs_sample_payload (event, &raw, &length))
    goto malformed;

  if (!_sysprof_gpu_jobs_read_sample (format,
                                      raw,
                                      length,
                                      &key.context,
                                      &key.seqno,
                                      &dev,
                                      &ring))
    goto malformed;

  if (self->started_at == 0 ||
      event->tracepoint.time < self->started_at ||
      (self->ended_at &&
       event->tracepoint.time > self->ended_at))
    goto finish;

  self->valid_events++;

  if (!(job = g_hash_table_lookup (self->jobs, &key)))
    {
      GpuKey *stored_key;

      if (g_hash_table_size (self->jobs) >= 8192)
        {
          GHashTableIter iter;
          gpointer old_key;

          g_hash_table_iter_init (&iter, self->jobs);
          if (g_hash_table_iter_next (&iter, &old_key, NULL))
            g_hash_table_iter_remove (&iter);

          if (self->dropped_jobs++ == 0)
            _sysprof_recording_diagnostic (self->recording,
                                           "GPU Jobs",
                                           "Unmatched job limit reached; jobs were discarded");
        }

      stored_key = g_memdup2 (&key, sizeof key);
      job = g_new0 (GpuJob, 1);
      g_hash_table_insert (self->jobs, stored_key, job);
    }

  if (dev != NULL && job->dev == NULL)
    job->dev = g_steal_pointer (&dev);

  if (ring != NULL && job->ring == NULL)
    job->ring = g_steal_pointer (&ring);

  ready = _sysprof_gpu_job_add_stage (&job->stages, stage, event->tracepoint.time);

  if (ready & 1)
    gpu_emit_span (self, job, &key, 0, cpu);

  if (ready & 2)
    gpu_emit_span (self, job, &key, 1, cpu);

  if (job->stages.emitted == 3)
    g_hash_table_remove (self->jobs, &key);

finish:
  g_free (dev);
  g_free (ring);
  return;

malformed:
  if (self->bad_samples++ == 0)
    _sysprof_recording_diagnostic (self->recording,
                                   "GPU Jobs",
                                   "Malformed DRM scheduler sample");
  g_free (dev);
  g_free (ring);
}

static void
gpu_queue_cb (const SysprofPerfEvent *e,
              guint                   cpu,
              gpointer                data)
{
  gpu_event (e, cpu, data, 0);
}

static void
gpu_run_cb (const SysprofPerfEvent *e,
            guint                   cpu,
            gpointer                data)
{
  gpu_event (e, cpu, data, 1);
}

static void
gpu_done_cb (const SysprofPerfEvent *e,
             guint                   cpu,
             gpointer                data)
{
  gpu_event (e, cpu, data, 2);
}

static DexFuture *
gpu_prepare_fiber (gpointer data)
{
  SysprofGpuJobs *self = data;
  const char *names[] = { "drm_sched_job_queue", "drm_sched_job_run", "drm_sched_job_done" };
  SysprofPerfEventCallback callbacks[] = { gpu_queue_cb, gpu_run_cb, gpu_done_cb };
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_GPU_JOBS (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));

  connection = _sysprof_recording_dup_connection (self->recording);

  for (guint stage = 0; stage < 3; stage++)
    {
      g_autofree char *path = g_strdup_printf ("/sys/kernel/debug/tracing/events/gpu_scheduler/%s/format", names[stage]);
      g_autoptr(GBytes) bytes = NULL;
      g_autofree char *format = NULL;

      if (!(bytes = dex_await_boxed (sysprof_get_proc_file_bytes (connection, path), &error)))
        goto unavailable;

      format = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));

      if (!_sysprof_gpu_jobs_parse_format (format, &self->formats[stage], stage != 2))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_DATA,
                       "Unsupported format for %s",
                       names[stage]);
          goto unavailable;
        }
    }

  if (!(cpus = _sysprof_get_online_cpus (&error)))
    goto unavailable;

  for (guint stage = 0; stage < 3; stage++)
    {
      for (guint i = 0; i < cpus->len; i++)
        {
          struct perf_event_attr attr = { 0 };
          guint cpu = g_array_index (cpus, guint, i);
          g_autoptr(SysprofPerfEventStream) stream = NULL;
          g_autoptr(DexFuture) future = NULL;

          attr.type = PERF_TYPE_TRACEPOINT;
          attr.sample_type = (PERF_SAMPLE_RAW |
                              PERF_SAMPLE_IP |
                              PERF_SAMPLE_TID |
                              PERF_SAMPLE_IDENTIFIER |
                              PERF_SAMPLE_TIME);
          attr.config = self->formats[stage].id;
          attr.sample_period = 1;
#ifdef HAVE_PERF_CLOCKID
          attr.clockid = sysprof_clock;
          attr.use_clockid = 1;
#endif
          attr.size = sizeof attr;
          future = sysprof_perf_event_stream_new (connection, &attr, 16, cpu, -1, 0,
                                                  callbacks[stage], self, NULL);

          if (!(stream = dex_await_object (dex_ref (future), &error)))
            {
              g_prefix_error (&error, "%s on CPU %u: ", names[stage], cpu);
              goto unavailable;
            }

          if (!sysprof_perf_event_stream_enable (stream, &error))
            {
              g_prefix_error (&error, "%s on CPU %u: ", names[stage], cpu);
              goto unavailable;
            }

          g_ptr_array_add (self->streams, g_steal_pointer (&stream));
        }
    }

  self->tracepoints_enabled = TRUE;

  return dex_future_new_for_boolean (TRUE);

unavailable:
  _sysprof_recording_diagnostic (self->recording,
                                 "GPU Jobs",
                                 "DRM scheduler tracing unavailable: %s",
                                 error->message);

  for (guint i = 0; i < self->streams->len; i++)
    sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);

  g_ptr_array_set_size (self->streams, 0);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
gpu_prepare (SysprofInstrument *instrument,
             SysprofRecording  *recording)
{
  SysprofGpuJobs *self = SYSPROF_GPU_JOBS (instrument);

  g_assert (SYSPROF_IS_GPU_JOBS (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL,
                              0,
                              gpu_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
gpu_record_fiber (gpointer data)
{
  SysprofGpuJobs *self = data;

  g_assert (SYSPROF_IS_GPU_JOBS (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));
  g_assert (DEX_IS_FUTURE (self->cancellable));

  self->started_at = SYSPROF_CAPTURE_CURRENT_TIME;
  dex_await (dex_ref (self->cancellable), NULL);
  self->ended_at = SYSPROF_CAPTURE_CURRENT_TIME;

  for (guint i = 0; i < self->streams->len; i++)
    sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);

  g_ptr_array_set_size (self->streams, 0);

  if (self->tracepoints_enabled && self->valid_events == 0)
    _sysprof_recording_diagnostic (self->recording,
                                   "GPU Jobs",
                                   "No DRM scheduler job events were observed; the active GPU driver may not use the DRM scheduler");

  if (g_hash_table_size (self->jobs) > 0)
    _sysprof_recording_diagnostic (self->recording,
                                   "GPU Jobs",
                                   "%u incomplete jobs at capture end",
                                   g_hash_table_size (self->jobs));

  if (self->bad_samples > 0)
    _sysprof_recording_diagnostic (self->recording,
                                   "GPU Jobs",
                                   "%u malformed DRM scheduler samples",
                                   self->bad_samples);

  if (self->dropped_jobs > 0)
    _sysprof_recording_diagnostic (self->recording,
                                   "GPU Jobs",
                                   "%u unmatched jobs discarded after reaching the limit",
                                   self->dropped_jobs);

  g_hash_table_remove_all (self->jobs);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
gpu_record (SysprofInstrument *instrument,
            SysprofRecording  *recording,
            GCancellable      *cancellable)
{
  SysprofGpuJobs *self = SYSPROF_GPU_JOBS (instrument);

  g_assert (SYSPROF_IS_GPU_JOBS (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (recording == self->recording);
  g_assert (self->cancellable == NULL);

  self->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              gpu_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
gpu_dispose (GObject *object)
{
  SysprofGpuJobs *self = SYSPROF_GPU_JOBS (object);

  if (self->streams != NULL)
    {
      for (guint i = 0; i < self->streams->len; i++)
        sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);
      g_clear_pointer (&self->streams, g_ptr_array_unref);
    }

  g_clear_pointer (&self->jobs, g_hash_table_unref);
  g_clear_object (&self->recording);
  dex_clear (&self->cancellable);

  G_OBJECT_CLASS (sysprof_gpu_jobs_parent_class)->dispose (object);
}

static void
sysprof_gpu_jobs_class_init (SysprofGpuJobsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = gpu_dispose;

  instrument_class->prepare = gpu_prepare;
  instrument_class->record = gpu_record;
}

static void
sysprof_gpu_jobs_init (SysprofGpuJobs *self)
{
  self->streams = g_ptr_array_new_with_free_func (g_object_unref);
  self->jobs = g_hash_table_new_full (gpu_key_hash, gpu_key_equal, g_free, gpu_job_free);
}

/**
 * sysprof_gpu_jobs_new:
 *
 * Creates an instrument for DRM scheduler job timing.
 *
 * Records queue-wait and in-flight spans as marks in the "GPU Jobs" group.
 * In-flight spans measure scheduler fence lifetime.
 *
 * Returns: (transfer full): a new instrument
 *
 * Since: 52
 */
SysprofInstrument *
sysprof_gpu_jobs_new (void)
{
  return g_object_new (SYSPROF_TYPE_GPU_JOBS, NULL);
}
