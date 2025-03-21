/* sysprof-scheduler-details.c
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

#include "sysprof-instrument-private.h"
#include "sysprof-perf-event-stream-private.h"
#include "sysprof-scheduler-details.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#include "line-reader-private.h"

struct _SysprofSchedulerDetails
{
  SysprofInstrument  parent_instance;

  GDBusConnection   *connection;
  SysprofRecording  *recording;
  DexFuture         *cancellable;
  GPtrArray         *streams;
  gint64            *last_switch_times;

  gint64             started_at;
  gint64             ended_at;

  gint64             sched_switch_id;
  gint64             prev_comm_offset;
  gint64             prev_pid_offset;
  gint64             prev_state_offset;
};

struct _SysprofSchedulerDetailsClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofSchedulerDetails, sysprof_scheduler_details, SYSPROF_TYPE_INSTRUMENT)

static void
sysprof_scheduler_details_event_cb (const SysprofPerfEvent *event,
                                    guint                   cpu,
                                    gpointer                user_data)
{
  SysprofSchedulerDetails *self = user_data;
  const guint8 *raw;
  gint64 begin, end;
  glong prev_state;
  char prev_comm[16];
  char name[8];
  GPid prev_pid;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));

  if (event->header.type != PERF_RECORD_SAMPLE)
    return;

  begin = self->last_switch_times[cpu];
  end = event->tracepoint.time;

  if (begin == 0)
    goto finish;

  if (self->started_at == 0 || end < self->started_at)
    goto finish;

  if (self->ended_at != 0 && begin > self->ended_at)
    goto finish;

  raw = event->tracepoint.raw;

  memcpy (&prev_pid, raw + self->prev_pid_offset, sizeof prev_pid);
  memcpy (&prev_state, raw + self->prev_state_offset, sizeof prev_state);
  memcpy (&prev_comm[0], raw + self->prev_comm_offset, sizeof prev_comm);

  prev_comm[sizeof prev_comm-1] = 0;

  /* Ignore the kswapper events because they just clutter up the
   * timeline from things that would otherwise stand out.
   */
  if (memcmp (prev_comm, "swapper/", strlen ("swapper/")) == 0)
    goto finish;

  g_snprintf (name, sizeof name, "CPU %u", cpu);

  sysprof_capture_writer_add_mark (self->recording->writer,
                                   begin,
                                   cpu,
                                   event->tracepoint.pid,
                                   end - begin,
                                   "Scheduler",
                                   name,
                                   prev_comm);

finish:
  self->last_switch_times[cpu] = end;
}

static void
sysprof_scheduler_details_parse_field (SysprofSchedulerDetails  *self,
                                       char                    **parts)
{
  G_GNUC_UNUSED gboolean _signed = FALSE;
  gint64 offset = -1;
  gint64 size = 0;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (parts != NULL);

  for (guint i = 1; parts[i]; i++)
    {
      if (g_str_has_prefix (parts[i], "offset:"))
        offset = g_ascii_strtoll (parts[i] + strlen ("offset:"), NULL, 10);
      else if (g_str_has_prefix (parts[i], "size:"))
        size = g_ascii_strtoll (parts[i] + strlen ("size:"), NULL, 10);
      else if (g_str_has_prefix (parts[i], "signed:"))
        _signed = parts[i][strlen ("signed:")] == '1';
    }

  if (offset == -1)
    return;

  if (g_str_equal (parts[0], "field:char prev_comm[16];"))
    {
      if (size == 16)
        self->prev_comm_offset = offset;
    }
  else if (g_str_equal (parts[0], "field:pid_t prev_pid;"))
    {
      if (size == 4)
        self->prev_pid_offset = offset;
    }
  else if (g_str_equal (parts[0], "field:long prev_state;"))
    {
      if (size == sizeof (glong))
        self->prev_state_offset = offset;
    }
}

static gboolean
sysprof_scheduler_details_parse_format (SysprofSchedulerDetails *self,
                                        char                    *format)
{
  LineReader reader;
  char *line;
  gsize line_len;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (format != NULL);

  line_reader_init (&reader, format, -1);

  while ((line = line_reader_next (&reader, &line_len)))
    {
      line[line_len] = 0;

      if (g_str_has_prefix (line, "ID: "))
        {
          if (!(self->sched_switch_id = g_ascii_strtoll (line + strlen ("ID: "), NULL, 10)))
            return FALSE;
        }

      if (g_str_has_prefix (line, "\t"))
        {
          g_auto(GStrv) parts = g_strsplit (g_strstrip (line), "\t", 0);

          sysprof_scheduler_details_parse_field (self, parts);
        }
    }

  return self->sched_switch_id > 0 &&
         self->prev_comm_offset > 0 &&
         self->prev_state_offset > 0 &&
         self->prev_pid_offset > 0;
}

static DexFuture *
sysprof_scheduler_details_prepare_fiber (gpointer user_data)
{
  SysprofSchedulerDetails *self = user_data;
  g_autoptr(GPtrArray) futures = NULL;
  g_autoptr(GBytes) format_bytes = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *format = NULL;
  int n_cpu;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));

  if (!(format_bytes = dex_await_boxed (sysprof_get_proc_file_bytes (self->connection,
                                                                     "/sys/kernel/debug/tracing/events/sched/sched_switch/format"),
                                        &error)))
    goto handle_error;

  format = g_strndup (g_bytes_get_data (format_bytes, NULL),
                      g_bytes_get_size (format_bytes));

  if (!sysprof_scheduler_details_parse_format (self, format))
    {
      error = g_error_new_literal (G_IO_ERROR,
                                   G_IO_ERROR_INVALID_DATA,
                                   "Could not parse data in tracepoint format");
      goto handle_error;
    }

  futures = g_ptr_array_new_with_free_func (dex_unref);
  n_cpu = g_get_num_processors ();

  self->last_switch_times = g_new0 (gint64, n_cpu);

  for (int cpu = 0; cpu < n_cpu; cpu++)
    {
      struct perf_event_attr attr = {0};

      attr.type = PERF_TYPE_TRACEPOINT;
      attr.type = PERF_TYPE_TRACEPOINT;
      attr.sample_type = PERF_SAMPLE_RAW |
                         PERF_SAMPLE_IP |
                         PERF_SAMPLE_TID |
                         PERF_SAMPLE_IDENTIFIER |
                         PERF_SAMPLE_TIME;
      attr.config = self->sched_switch_id;
      attr.sample_period = 1;

#ifdef HAVE_PERF_CLOCKID
      attr.clockid = sysprof_clock;
      attr.use_clockid = 1;
#endif

      attr.size = sizeof attr;

      g_ptr_array_add (futures,
                       sysprof_perf_event_stream_new (self->connection,
                                                      &attr,
                                                      cpu,
                                                      -1,
                                                      0,
                                                      sysprof_scheduler_details_event_cb,
                                                      self,
                                                      NULL));
    }

  /* Create perf stream per-cpu to get sched:sched_switch */
  if (!dex_await (dex_future_allv ((DexFuture **)futures->pdata, futures->len), &error))
    goto handle_error;

  for (int i = 0; i < futures->len; i++)
    {
      DexFuture *future = g_ptr_array_index (futures, i);
      g_autoptr(SysprofPerfEventStream) stream = dex_await_object (dex_ref (future), NULL);

      g_assert (stream != NULL);
      g_assert (SYSPROF_IS_PERF_EVENT_STREAM (stream));

      /* We'll start processing these once start_time is set
       * for recording. That way we don't miss anything when
       * racing to record.
       */
      if (sysprof_perf_event_stream_enable (stream, NULL))
        g_ptr_array_add (self->streams, g_object_ref (stream));
    }

  return dex_future_new_for_boolean (TRUE);

handle_error:
  g_assert (error != NULL);

  _sysprof_recording_diagnostic (self->recording,
                                 "Scheduler",
                                 "Failed to register scheduler tracepoints: %s",
                                 error->message);

  return dex_future_new_for_error (g_steal_pointer (&error));
}

static DexFuture *
sysprof_scheduler_details_prepare (SysprofInstrument *instrument,
                                   SysprofRecording  *recording)
{
  SysprofSchedulerDetails *self = (SysprofSchedulerDetails *)instrument;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_scheduler_details_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
sysprof_scheduler_details_record_fiber (gpointer user_data)
{
  SysprofSchedulerDetails *self = user_data;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));
  g_assert (DEX_IS_FUTURE (self->cancellable));

  self->started_at = SYSPROF_CAPTURE_CURRENT_TIME;
  dex_await (dex_ref (self->cancellable), NULL);
  self->ended_at = SYSPROF_CAPTURE_CURRENT_TIME;

  for (guint i = 0; i < self->streams->len; i++)
    {
      SysprofPerfEventStream *stream = g_ptr_array_index (self->streams, i);

      sysprof_perf_event_stream_disable (stream, NULL);
    }

  if (self->streams->len)
    g_ptr_array_remove_range (self->streams, 0, self->streams->len);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_scheduler_details_record (SysprofInstrument *instrument,
                                  SysprofRecording  *recording,
                                  GCancellable      *cancellable)
{
  SysprofSchedulerDetails *self = (SysprofSchedulerDetails *)instrument;

  g_assert (SYSPROF_IS_SCHEDULER_DETAILS (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (recording == self->recording);
  g_assert (self->cancellable == NULL);

  self->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_scheduler_details_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
sysprof_scheduler_details_set_connection (SysprofInstrument *instrument,
                                          GDBusConnection   *connection)
{
  SysprofSchedulerDetails *self = SYSPROF_SCHEDULER_DETAILS (instrument);

  g_set_object (&self->connection, connection);
}

static void
sysprof_scheduler_details_dispose (GObject *object)
{
  SysprofSchedulerDetails *self = (SysprofSchedulerDetails *)object;

  if (self->streams != NULL)
    {
      for (guint i = 0; i < self->streams->len; i++)
        {
          SysprofPerfEventStream *stream = g_ptr_array_index (self->streams, i);
          sysprof_perf_event_stream_disable (stream, NULL);
        }

      g_clear_pointer (&self->streams, g_ptr_array_unref);
    }

  g_clear_object (&self->connection);
  g_clear_object (&self->recording);
  g_clear_pointer (&self->last_switch_times, g_free);
  dex_clear (&self->cancellable);

  G_OBJECT_CLASS (sysprof_scheduler_details_parent_class)->dispose (object);
}

static void
sysprof_scheduler_details_class_init (SysprofSchedulerDetailsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_scheduler_details_dispose;

  instrument_class->prepare = sysprof_scheduler_details_prepare;
  instrument_class->record = sysprof_scheduler_details_record;
  instrument_class->set_connection = sysprof_scheduler_details_set_connection;
}

static void
sysprof_scheduler_details_init (SysprofSchedulerDetails *self)
{
  self->sched_switch_id = -1;
  self->prev_comm_offset = -1;
  self->prev_pid_offset = -1;
  self->prev_state_offset = -1;
  self->streams = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofInstrument *
sysprof_scheduler_details_new (void)
{
  return g_object_new (SYSPROF_TYPE_SCHEDULER_DETAILS, NULL);
}
