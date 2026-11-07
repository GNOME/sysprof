/* sysprof-vblank.c
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
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "sysprof-instrument-private.h"
#include "sysprof-perf-event-stream-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"
#include "sysprof-vblank-private.h"
#include "sysprof-vblank.h"

struct _SysprofVblank
{
  SysprofInstrument    parent_instance;
  GDBusConnection     *connection;
  SysprofRecording    *recording;
  DexFuture           *cancellable;
  GPtrArray           *streams;
  SysprofVblankFormat  format;
  GMutex               clock_lock;
  gint64               monotonic_offset;
  gint64               raw_offset;
  gint64               calibrated_at;
  gint64               started_at;
  gint64               ended_at;
  guint                bad_samples;
  guint                bad_clocks;
  guint                calibration_source;
};

struct _SysprofVblankClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofVblank, sysprof_vblank, SYSPROF_TYPE_INSTRUMENT)

static gboolean
parse_field (char               *line,
             SysprofVblankField *field,
             const char         *name,
             guint               size)
{
  g_auto(GStrv) parts = g_strsplit (g_strstrip (line), "\t", 0);
  g_autofree char *suffix = NULL;
  guint offset = G_MAXUINT;
  guint parsed_size = 0;

  suffix = g_strdup_printf (" %s;", name);

  if (parts[0] == NULL || !g_str_has_suffix (parts[0], suffix))
    return FALSE;

  for (guint i = 1; parts[i] != NULL; i++)
    {
      char *part = g_strstrip (parts[i]);

      if (g_str_has_prefix (part, "offset:"))
        {
          char *end;
          guint64 value;

          errno = 0;
          value = g_ascii_strtoull (part + 7, &end, 10);

          if (errno != 0 || end == part + 7 || (*end != 0 && *end != ';') ||
              value > G_MAXUINT)
            return FALSE;

          offset = value;
        }
      else if (g_str_has_prefix (part, "size:"))
        {
          char *end;
          guint64 value;

          errno = 0;
          value = g_ascii_strtoull (part + 5, &end, 10);

          if (errno != 0 || end == part + 5 || (*end != 0 && *end != ';') ||
              value > G_MAXUINT)
            return FALSE;

          parsed_size = value;
        }
    }

  if (offset == G_MAXUINT || parsed_size != size || offset > 4096 - size)
    return FALSE;

  field->offset = offset;
  field->size = size;

  return TRUE;
}

gboolean
_sysprof_vblank_parse_format (const char          *text,
                              SysprofVblankFormat *format)
{
  g_auto(GStrv) lines = NULL;

  g_return_val_if_fail (text != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  *format = (SysprofVblankFormat) { 0 };
  lines = g_strsplit (text, "\n", 0);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      char *line = lines[i];

      if (g_str_has_prefix (line, "ID: "))
        format->id = (guint)g_ascii_strtoull (line + 4, NULL, 10);
      else if (strstr (line, "field:") != NULL)
        {
          parse_field (line, &format->crtc, "crtc", 4);
          parse_field (line, &format->sequence, "seq", 4);
          parse_field (line, &format->time, "time", 8);
          parse_field (line, &format->high_prec, "high_prec", 1);
        }
    }

  return format->id > 0 && format->crtc.size && format->sequence.size &&
         format->time.size && format->high_prec.size;
}

static gboolean
field_fits (const SysprofVblankField *field,
            gsize                     length)
{
  return field->size != 0 && field->offset <= length &&
         field->size <= length - field->offset;
}

gboolean
_sysprof_vblank_read_sample (const SysprofVblankFormat *format,
                             const guint8              *raw,
                             gsize                      length,
                             gint32                    *crtc,
                             guint32                   *sequence,
                             gint64                    *time,
                             gboolean                  *high_prec)
{
  guint8 precision;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (raw != NULL, FALSE);

  if (!field_fits (&format->crtc, length) ||
      !field_fits (&format->sequence, length) ||
      !field_fits (&format->time, length) ||
      !field_fits (&format->high_prec, length))
    return FALSE;

  memcpy (crtc, raw + format->crtc.offset, 4);
  memcpy (sequence, raw + format->sequence.offset, 4);
  memcpy (time, raw + format->time.offset, 8);
  memcpy (&precision, raw + format->high_prec.offset, 1);
  *high_prec = precision != 0;

  return *time > 0;
}

gboolean
_sysprof_vblank_convert_time (gint64  payload_time,
                              gint64  observation_time,
                              gint64  monotonic_offset,
                              gint64  raw_offset,
                              gint64 *capture_time)
{
  gint64 monotonic;
  gint64 raw;
  long double monotonic_distance;
  long double raw_distance;
  long double clock_distance;
  gboolean monotonic_near;
  gboolean raw_near;

  g_return_val_if_fail (capture_time != NULL, FALSE);

  if (payload_time <= 0 ||
      (long double)payload_time + monotonic_offset > G_MAXINT64 ||
      (long double)payload_time + monotonic_offset < G_MININT64 ||
      (long double)payload_time + raw_offset > G_MAXINT64 ||
      (long double)payload_time + raw_offset < G_MININT64)
    return FALSE;

  monotonic = payload_time + monotonic_offset;
  raw = payload_time + raw_offset;
  monotonic_distance = (long double)monotonic - observation_time;
  raw_distance = (long double)raw - observation_time;
  clock_distance = (long double)monotonic_offset - raw_offset;

  if (monotonic_distance < 0)
    monotonic_distance = -monotonic_distance;

  if (raw_distance < 0)
    raw_distance = -raw_distance;

  if (clock_distance < 0)
    clock_distance = -clock_distance;

  monotonic_near = monotonic_distance <= 50000000;
  raw_near = raw_distance <= 50000000;

  if (clock_distance <= 1000000)
    raw_near = FALSE;

  if (monotonic_near == raw_near)
    return FALSE;

  *capture_time = monotonic_near ? monotonic : raw;

  return TRUE;
}

gboolean
_sysprof_vblank_read_lost (const SysprofPerfEvent *event,
                           guint64                *lost)
{
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (lost != NULL, FALSE);

  if (event->header.type != PERF_RECORD_LOST || event->header.size < sizeof event->lost)
    return FALSE;

  *lost = event->lost.lost;

  return TRUE;
}

static gint64
clock_time (clockid_t clock_id)
{
  struct timespec ts;

  if (clock_gettime (clock_id, &ts) != 0)
    return 0;

  return (gint64)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void
calibrate_clocks (SysprofVblank *self)
{
  gint64 before_monotonic = SYSPROF_CAPTURE_CURRENT_TIME;
  gint64 monotonic = clock_time (CLOCK_MONOTONIC);
  gint64 after_monotonic = SYSPROF_CAPTURE_CURRENT_TIME;
  gint64 before_raw = SYSPROF_CAPTURE_CURRENT_TIME;
  gint64 raw = clock_time (CLOCK_MONOTONIC_RAW);
  gint64 after_raw = SYSPROF_CAPTURE_CURRENT_TIME;

  g_mutex_lock (&self->clock_lock);
  self->monotonic_offset = (before_monotonic +
                            (after_monotonic - before_monotonic) / 2) - monotonic;
  self->raw_offset = (before_raw + (after_raw - before_raw) / 2) - raw;
  self->calibrated_at = after_raw;
  g_mutex_unlock (&self->clock_lock);
}

static gboolean
calibrate_clocks_timeout (gpointer user_data)
{
  calibrate_clocks (user_data);

  return G_SOURCE_CONTINUE;
}

static void
vblank_event_cb (const SysprofPerfEvent *event,
                 guint                   cpu,
                 gpointer                user_data)
{
  SysprofVblank *self = user_data;
  guint32 sequence;
  gint32 crtc;

  gint64 time, converted;
  gint64 monotonic_offset, raw_offset, calibrated_at;
  gboolean high_prec;
  g_autofree char *details = NULL;
  guint64 lost;

  if (event->header.type == PERF_RECORD_LOST)
    {
      if (_sysprof_vblank_read_lost (event, &lost))
        _sysprof_recording_diagnostic (self->recording,
                                       "DRM VBlank",
                                       "Lost %" G_GUINT64_FORMAT " perf records on CPU %u",
                                       lost,
                                       cpu);

      return;
    }

  if (event->header.type != PERF_RECORD_SAMPLE)
    return;

  if (event->header.size < offsetof (SysprofPerfEventTracepoint, raw) ||
      event->tracepoint.raw_size > event->header.size -
                                   offsetof (SysprofPerfEventTracepoint, raw) ||
      !_sysprof_vblank_read_sample (&self->format,
                                    event->tracepoint.raw,
                                    event->tracepoint.raw_size,
                                    &crtc,
                                    &sequence,
                                    &time,
                                    &high_prec))
    {
      if (self->bad_samples++ == 0)
        _sysprof_recording_diagnostic (self->recording,
                                       "DRM VBlank",
                                       "Malformed drm_vblank_event sample");

      return;
    }

  if (self->started_at == 0 ||
      event->tracepoint.time < self->started_at ||
      (self->ended_at != 0 &&
       event->tracepoint.time > self->ended_at))
    return;

  g_mutex_lock (&self->clock_lock);
  calibrated_at = self->calibrated_at;
  g_mutex_unlock (&self->clock_lock);

  if (SYSPROF_CAPTURE_CURRENT_TIME - calibrated_at >= G_GINT64_CONSTANT (60000000000))
    calibrate_clocks (self);

  g_mutex_lock (&self->clock_lock);
  monotonic_offset = self->monotonic_offset;
  raw_offset = self->raw_offset;
  g_mutex_unlock (&self->clock_lock);

  if (!_sysprof_vblank_convert_time (time,
                                     event->tracepoint.time,
                                     monotonic_offset,
                                     raw_offset,
                                     &converted))
    {
      if (self->bad_clocks++ == 0)
        _sysprof_recording_diagnostic (self->recording,
                                       "DRM VBlank",
                                       "Timestamp clock is ambiguous or differs from perf by "
                                       "over 50 ms");

      return;
    }

  details = g_strdup_printf ("sequence=%u, CRTC=%d, high_prec=%s, observed=%" G_GINT64_FORMAT,
                             sequence,
                             crtc,
                             high_prec ? "yes" : "no",
                             event->tracepoint.time);
  sysprof_capture_writer_add_mark (self->recording->writer,
                                   converted,
                                   cpu,
                                   -1,
                                   0,
                                   "DRM VBlank",
                                   "VBlank",
                                   details);
}

static DexFuture *
vblank_prepare_fiber (gpointer user_data)
{
  SysprofVblank *self = user_data;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GPtrArray) futures = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *format = NULL;
  g_autofree char *nvidia = NULL;

  if ((nvidia = dex_await_string (_sysprof_load_file ("/sys/module/nvidia_drm/parameters/vblank"), NULL)) &&
      (nvidia[0] == 'N' || nvidia[0] == '0'))
    _sysprof_recording_diagnostic (self->recording,
                                   "DRM VBlank",
                                   "nvidia-drm.vblank is disabled; set nvidia-drm.vblank=1");

  bytes = dex_await_boxed (sysprof_get_proc_file_bytes (self->connection,
                                                        "/sys/kernel/tracing/events/drm/drm_vblank_event/format"),
                           &error);

  if (bytes == NULL)
    {
      g_clear_error (&error);
      bytes = dex_await_boxed (sysprof_get_proc_file_bytes (self->connection,
                                                            "/sys/kernel/debug/tracing/events/drm/drm_vblank_event/format"),
                               &error);
    }

  if (bytes == NULL)
    goto failure;

  format = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));

  if (!_sysprof_vblank_parse_format (format, &self->format))
    {
      g_set_error_literal (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Could not parse drm_vblank_event tracepoint format");
      goto failure;
    }

  if (!(cpus = dex_await_boxed (_sysprof_list_online_cpus (), &error)))
    goto failure;

  futures = g_ptr_array_new_with_free_func (dex_unref);
  calibrate_clocks (self);

  for (guint i = 0; i < cpus->len; i++)
    {
      struct perf_event_attr attr = { 0 };

      guint cpu = g_array_index (cpus, guint, i);

      attr.type = PERF_TYPE_TRACEPOINT;
      attr.config = self->format.id;
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
                                                      64,
                                                      cpu,
                                                      -1,
                                                      0,
                                                      vblank_event_cb,
                                                      self,
                                                      NULL));
    }

  if (!dex_await (dex_future_allv ((DexFuture **)futures->pdata, futures->len), &error))
    goto failure;

  for (guint i = 0; i < futures->len; i++)
    {
      g_autoptr(SysprofPerfEventStream) stream =
        dex_await_object (dex_ref (g_ptr_array_index (futures, i)), NULL);

      if (sysprof_perf_event_stream_enable (stream, &error))
        g_ptr_array_add (self->streams, g_object_ref (stream));
      else
        goto failure;
    }

  return dex_future_new_for_boolean (TRUE);

failure:
  _sysprof_recording_diagnostic (self->recording,
                                 "DRM VBlank",
                                 "Cannot record drm_vblank_event: %s",
                                 error->message);

  return dex_future_new_for_error (g_steal_pointer (&error));
}

static DexFuture *
vblank_prepare (SysprofInstrument *instrument,
                SysprofRecording  *recording)
{
  SysprofVblank *self = SYSPROF_VBLANK (instrument);

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL,
                              0,
                              vblank_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
vblank_record_fiber (gpointer user_data)
{
  SysprofVblank *self = user_data;

  self->started_at = SYSPROF_CAPTURE_CURRENT_TIME;
  self->calibration_source = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                                         60,
                                                         calibrate_clocks_timeout,
                                                         g_object_ref (self),
                                                         g_object_unref);

  dex_await (dex_ref (self->cancellable), NULL);

  self->ended_at = SYSPROF_CAPTURE_CURRENT_TIME;
  g_clear_handle_id (&self->calibration_source, g_source_remove);

  for (guint i = 0; i < self->streams->len; i++)
    sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);

  g_ptr_array_set_size (self->streams, 0);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
vblank_record (SysprofInstrument *instrument,
               SysprofRecording  *recording,
               GCancellable      *cancellable)
{
  SysprofVblank *self = SYSPROF_VBLANK (instrument);

  self->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              vblank_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
vblank_set_connection (SysprofInstrument *instrument,
                       GDBusConnection   *connection)
{
  SysprofVblank *self = SYSPROF_VBLANK (instrument);

  g_set_object (&self->connection, connection);
}

static void
vblank_dispose (GObject *object)
{
  SysprofVblank *self = SYSPROF_VBLANK (object);

  if (self->streams != NULL)
    {
      for (guint i = 0; i < self->streams->len; i++)
        sysprof_perf_event_stream_disable (g_ptr_array_index (self->streams, i), NULL);
    }

  g_clear_handle_id (&self->calibration_source, g_source_remove);
  g_clear_pointer (&self->streams, g_ptr_array_unref);
  g_clear_object (&self->connection);
  g_clear_object (&self->recording);
  dex_clear (&self->cancellable);

  G_OBJECT_CLASS (sysprof_vblank_parent_class)->dispose (object);
}

static void
vblank_finalize (GObject *object)
{
  SysprofVblank *self = SYSPROF_VBLANK (object);

  g_mutex_clear (&self->clock_lock);

  G_OBJECT_CLASS (sysprof_vblank_parent_class)->finalize (object);
}

static void
sysprof_vblank_class_init (SysprofVblankClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = vblank_dispose;
  object_class->finalize = vblank_finalize;

  instrument_class->prepare = vblank_prepare;
  instrument_class->record = vblank_record;
  instrument_class->set_connection = vblank_set_connection;
}

static void
sysprof_vblank_init (SysprofVblank *self)
{
  self->streams = g_ptr_array_new_with_free_func (g_object_unref);
  g_mutex_init (&self->clock_lock);
}

/**
 * sysprof_vblank_new:
 *
 * Creates an instrument recording DRM vblank event timestamps.
 *
 * Returns: (transfer full): a new #SysprofInstrument
 */
SysprofInstrument *
sysprof_vblank_new (void)
{
  return g_object_new (SYSPROF_TYPE_VBLANK, NULL);
}
