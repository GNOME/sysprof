/* sysprof-memory-sampler.c
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

#if defined(__i386__) || defined(__x86_64__)
# include <cpuid.h>
#endif

#include "sysprof-instrument-private.h"
#include "sysprof-memory-sampler.h"
#include "sysprof-memory-sampler-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#define MEMORY_PERIOD 1000
#define MEMORY_LATENCY 30

typedef struct
{
  GWeakRef                recording;
  SysprofPerfEventStream *stream;
  GKeyFile               *description;
  guint64                 event_id;
  guint64                 samples;
  guint64                 lost;
  guint64                 malformed;
  guint64                 throttles;
  int                     cpu;
} MemoryStream;

struct _SysprofMemorySampler
{
  SysprofInstrument  parent_instance;
  GPtrArray         *streams;
};

struct _SysprofMemorySamplerClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofMemorySampler, sysprof_memory_sampler, SYSPROF_TYPE_INSTRUMENT)

static void
memory_stream_free (gpointer data)
{
  MemoryStream *stream = data;

  g_clear_object (&stream->stream);
  g_clear_pointer (&stream->description, g_key_file_unref);
  g_weak_ref_clear (&stream->recording);
  g_free (stream);
}

static void memory_diagnostic (SysprofRecording *recording,
                               const char       *format,
                               ...) G_GNUC_PRINTF (2, 3);

static void
memory_diagnostic (SysprofRecording *recording,
                   const char       *format,
                   ...)
{
  g_autofree char *message = NULL;
  va_list args;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (format != NULL);

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  sysprof_capture_writer_add_log (_sysprof_recording_writer (recording),
                                  SYSPROF_CAPTURE_CURRENT_TIME, -1, -1,
                                  G_LOG_LEVEL_MESSAGE, "Memory Sampler", message);

  _sysprof_recording_diagnostic (recording, "Memory Sampler", "%s", message);
}

static void
write_description (MemoryStream     *stream,
                   SysprofRecording *recording)
{
  SysprofCaptureWriter *writer = _sysprof_recording_writer (recording);
  g_autofree char *payload = NULL;
  char id[40];

  g_assert (stream != NULL);

  g_snprintf (id,
              sizeof id,
              SYSPROF_CAPTURE_MEMORY_EVENT_PREFIX "%016" G_GINT64_MODIFIER "x",
              stream->event_id);

  payload = g_key_file_to_data (stream->description, NULL, NULL);

  if (!sysprof_capture_writer_add_metadata (writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            stream->cpu,
                                            -1,
                                            id,
                                            payload,
                                            -1))
    memory_diagnostic (recording,
                       "Failed to write event description for CPU %d",
                       stream->cpu);
}

static gboolean
record_process_event (SysprofRecording       *recording,
                      const SysprofPerfEvent *event,
                      guint                   cpu)
{
  SysprofCaptureWriter *writer = _sysprof_recording_writer (recording);
  const char *string = NULL;
  gsize offset = 0;
  gint64 time;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (event != NULL);

  switch (event->header.type)
    {
    case PERF_RECORD_COMM:
      offset = G_STRUCT_OFFSET (SysprofPerfEventComm, comm);
      break;

    case PERF_RECORD_MMAP:
      offset = G_STRUCT_OFFSET (SysprofPerfEventMmap, filename);
      break;

    case PERF_RECORD_MMAP2:
      offset = G_STRUCT_OFFSET (SysprofPerfEventMmap2, filename);
      break;

    case PERF_RECORD_FORK:
      if (event->header.size < sizeof (SysprofPerfEventFork))
        return FALSE;
      return sysprof_capture_writer_add_fork (writer, event->fork.time, cpu,
                                               event->fork.ptid, event->fork.tid);

    case PERF_RECORD_EXIT:
      if (event->header.size < sizeof (SysprofPerfEventExit))
        return FALSE;
      if (event->exit.pid == event->exit.tid)
        return sysprof_capture_writer_add_exit (writer, event->exit.time, cpu, event->exit.pid);
      return TRUE;

    default:
      return TRUE;
    }

  if (event->header.size <= offset + 24)
    return FALSE;

  string = (const char *)event + offset;

  if (memchr (string, 0, event->header.size - offset - 24) == NULL)
    return FALSE;

  memcpy (&time, (const guint8 *)event + event->header.size - 16, sizeof time);

  if (event->header.type == PERF_RECORD_COMM)
    {
      if (event->comm.pid == event->comm.tid)
        {
          _sysprof_recording_follow_process (recording, event->comm.pid, string);

          return sysprof_capture_writer_add_process (writer, time, cpu, event->comm.pid, string);
        }

      return TRUE;
    }

  if (event->header.type == PERF_RECORD_MMAP)
    return sysprof_capture_writer_add_map (writer, time, cpu, event->mmap.pid,
                                            event->mmap.addr, event->mmap.addr + event->mmap.len,
                                            event->mmap.pgoff, 0, string);

  return sysprof_capture_writer_add_map (writer, time, cpu, event->mmap2.pid,
                                          event->mmap2.addr, event->mmap2.addr + event->mmap2.len,
                                          event->mmap2.pgoff, event->mmap2.ino, string);
}

static void
memory_event_cb (const SysprofPerfEvent *event,
                 guint                   cpu,
                 gpointer                user_data)
{
  MemoryStream *stream = user_data;
  g_autoptr(SysprofRecording) recording = g_weak_ref_get (&stream->recording);
  SysprofCaptureWriter *writer;

  g_assert (event != NULL);
  g_assert (stream != NULL);

  if (recording == NULL)
    return;

  writer = _sysprof_recording_writer (recording);

  switch (event->header.type)
    {
    case PERF_RECORD_SAMPLE:
      {
        SysprofCaptureMemorySample sample;
        SysprofCaptureAddress stack[SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK];

        if (!_sysprof_memory_sample_decode (event,
                                            event->header.size,
                                            SYSPROF_MEMORY_SAMPLE_TYPE,
                                            SYSPROF_MEMORY_READ_FORMAT,
                                            &sample,
                                            stack))
          {
            stream->malformed++;
            break;
          }

        if (recording->start_time == 0 ||
            sample.frame.time < recording->start_time * 1000)
          break;

        sample.frame.cpu = cpu;
        sample.event_id = stream->event_id;

        if (sysprof_capture_writer_add_memory_sample (writer, &sample, stack))
          stream->samples++;
        else
          stream->lost++;

        break;
      }

    case PERF_RECORD_LOST:
      if (event->header.size >= sizeof (SysprofPerfEventLost))
        stream->lost += event->lost.lost;
      else
        stream->malformed++;
      break;

    case PERF_RECORD_LOST_SAMPLES:
      {
        guint64 lost;

        if (event->header.size < sizeof event->header + sizeof lost)
          {
            stream->malformed++;
          }
        else
          {
            memcpy (&lost, (const guint8 *)event + sizeof event->header, sizeof lost);
            stream->lost += lost;
          }

        break;
      }

    case PERF_RECORD_COMM:
    case PERF_RECORD_MMAP:
    case PERF_RECORD_MMAP2:
    case PERF_RECORD_FORK:
    case PERF_RECORD_EXIT:
      if (!record_process_event (recording, event, cpu))
        stream->malformed++;
      break;

    case PERF_RECORD_THROTTLE:
      stream->throttles++;
      break;

    default:
      break;
    }
}

static gboolean
is_intel (void)
{
#if defined(__i386__) || defined(__x86_64__)
  unsigned int eax, ebx, ecx, edx;

  return __get_cpuid (0, &eax, &ebx, &ecx, &edx) &&
         ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e;
#else
  return FALSE;
#endif
}

typedef struct
{
  SysprofMemorySampler *self;
  SysprofRecording     *recording;
  DexFuture            *cancel;
} Operation;

static void
operation_free (Operation *op)
{
  g_clear_object (&op->self);
  g_clear_object (&op->recording);
  dex_clear (&op->cancel);
  g_free (op);
}

static DexFuture *
prepare_fiber (gpointer data)
{
  Operation *op = data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GArray) online = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GHashTable) covered = NULL;
  const char *name;
  guint attempted = 0;

  g_assert (op != NULL);

  connection = _sysprof_recording_dup_connection (op->recording);

  covered = g_hash_table_new (g_direct_hash, g_direct_equal);

  if (!is_intel ())
    {
      memory_diagnostic (op->recording,
                         "Memory sampling currently requires an Intel PMU; no coverage");

      return dex_future_new_for_boolean (TRUE);
    }

  if (!(online = _sysprof_get_online_cpus (&error)) ||
      !(dir = g_dir_open ("/sys/bus/event_source/devices", 0, &error)))
    {
      memory_diagnostic (op->recording,
                         "Cannot discover memory PMUs; no coverage: %s",
                         error->message);

      return dex_future_new_for_boolean (TRUE);
    }

  while ((name = g_dir_read_name (dir)))
    {
      g_autofree char *path = NULL;
      g_autoptr(GArray) cpus = NULL;
      struct perf_event_attr attr;

      if (!g_str_equal (name, "cpu") && !g_str_equal (name, "cpu_core") &&
          !g_str_equal (name, "cpu_atom"))
        continue;

      path = g_build_filename ("/sys/bus/event_source/devices", name, NULL);

      if (!_sysprof_memory_pmu_configure (path, MEMORY_LATENCY, &attr, &cpus, &error))
        {
          memory_diagnostic (op->recording,
                             "%s: no memory-sampling coverage: %s",
                             name,
                             error->message);

          g_clear_error (&error);
          continue;
        }

      attr.size = sizeof attr;
      attr.sample_type = SYSPROF_MEMORY_SAMPLE_TYPE;
      attr.read_format = SYSPROF_MEMORY_READ_FORMAT;
      attr.sample_period = MEMORY_PERIOD;
      attr.precise_ip = 2;
      attr.disabled = TRUE;
      attr.comm = TRUE;
      attr.task = TRUE;
      attr.mmap = TRUE;
      attr.mmap2 = TRUE;
      attr.sample_id_all = TRUE;
      attr.exclude_kernel = TRUE;
      attr.exclude_hv = TRUE;
      attr.exclude_idle = TRUE;
      attr.wakeup_events = 32;
#ifdef HAVE_PERF_CLOCKID
      attr.use_clockid = TRUE;
      attr.clockid = sysprof_clock;
#else
      memory_diagnostic (op->recording,
                         "%s: capture-clock selection is unavailable; no coverage", name);

      continue;
#endif

      for (guint i = 0; i < cpus->len; i++)
        {
          MemoryStream *stream;

          guint cpu = g_array_index (cpus, guint, i);
          gboolean is_online = FALSE;

          for (guint j = 0; j < online->len; j++)
            is_online |= cpu == g_array_index (online, guint, j);

          if (!is_online || cpu > G_MAXINT16)
            continue;

          attempted++;

          stream = g_new0 (MemoryStream, 1);
          g_weak_ref_init (&stream->recording, op->recording);
          stream->cpu = cpu;
          stream->description = g_key_file_new ();
          stream->event_id = sysprof_capture_writer_request_memory_event (_sysprof_recording_writer (op->recording));

          if (!(stream->stream = dex_await_object (sysprof_perf_event_stream_new (connection,
                                                                                  &attr,
                                                                                  32,
                                                                                  cpu,
                                                                                  -1,
                                                                                  PERF_FLAG_FD_CLOEXEC,
                                                                                  memory_event_cb,
                                                                                  stream,
                                                                                  NULL),
                                                   &error)))
            {
              memory_diagnostic (op->recording,
                                 "%s CPU %u: event open failed: %s",
                                 name,
                                 cpu,
                                 error->message);

              g_clear_error (&error);
              memory_stream_free (stream);
              continue;
            }

          if (!sysprof_perf_event_stream_enable (stream->stream, &error))
            {
              memory_diagnostic (op->recording,
                                 "%s CPU %u: event enable failed: %s",
                                 name,
                                 cpu,
                                 error->message);

              g_clear_error (&error);
              memory_stream_free (stream);
              continue;
            }

          g_key_file_set_integer (stream->description, "event", "version", 1);
          g_key_file_set_string (stream->description, "event", "name", "mem-loads");
          g_key_file_set_string (stream->description, "event", "mode", "intel-load-latency");
          g_key_file_set_string (stream->description, "event", "pmu", name);
          g_key_file_set_string (stream->description,
                                 "event",
                                 "core-kind",
                                 g_str_equal (name, "cpu_core") ? "performance" :
                                 g_str_equal (name, "cpu_atom") ? "efficiency" : "homogeneous");

          g_key_file_set_uint64 (stream->description, "event", "type", attr.type);
          g_key_file_set_uint64 (stream->description, "event", "config", attr.config);
          g_key_file_set_uint64 (stream->description, "event", "config1", attr.config1);
          g_key_file_set_uint64 (stream->description, "event", "config2", attr.config2);
          g_key_file_set_uint64 (stream->description, "event", "sample-type", attr.sample_type);
          g_key_file_set_uint64 (stream->description, "event", "read-format", attr.read_format);

          g_key_file_set_integer (stream->description, "event", "cpu", cpu);
          g_key_file_set_integer (stream->description, "event", "period", MEMORY_PERIOD);
          g_key_file_set_integer (stream->description, "event", "ldlat", MEMORY_LATENCY);
          g_key_file_set_integer (stream->description, "event", "requested-precise-ip", 2);
          g_key_file_set_integer (stream->description,
                                  "event",
                                  "effective-precise-ip",
                                  attr.precise_ip);

          g_key_file_set_integer (stream->description, "event", "clockid", sysprof_clock);
          g_key_file_set_boolean (stream->description, "event", "exclude-user", FALSE);
          g_key_file_set_boolean (stream->description, "event", "exclude-idle", TRUE);
          g_key_file_set_boolean (stream->description, "event", "sample-id-all", TRUE);
          g_key_file_set_boolean (stream->description, "event", "exclude-kernel", TRUE);
          g_key_file_set_boolean (stream->description, "event", "exclude-hv", TRUE);

          g_key_file_set_string (stream->description,
                                 "event",
                                 "weight-encoding",
                                 "PERF_SAMPLE_WEIGHT");

          g_key_file_set_string (stream->description, "event", "weight-unit", "cycles");
          g_key_file_set_string (stream->description,
                                 "event",
                                 "weight-meaning",
                                 "Intel reported load latency; may include pipeline delay");

          g_key_file_set_string (stream->description,
                                 "event",
                                 "data-src-encoding",
                                 "linux-perf-mem-data-src");

          g_key_file_set_string (stream->description,
                                 "event",
                                 "stack-mode",
                                 "perf-callchain-at-delivery; no user-space unwinding");

          write_description (stream, op->recording);

          g_hash_table_add (covered, GUINT_TO_POINTER (cpu));
          g_ptr_array_add (op->self->streams, stream);
        }
    }

  memory_diagnostic (op->recording,
                     "Intel load-latency sampling: %u/%u online CPUs covered, %u opens attempted; "
                     "period=%u, ldlat=%u, user-space accesses, perf delivery callchains. "
                     "Unavailable CPUs do not indicate zero expensive accesses.",
                     g_hash_table_size (covered),
                     online->len,
                     attempted,
                     MEMORY_PERIOD,
                     MEMORY_LATENCY);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
record_fiber (gpointer data)
{
  Operation *op = data;

  g_assert (op != NULL);

  dex_await (dex_ref (op->cancel), NULL);

  for (guint i = 0; i < op->self->streams->len; i++)
    {
      MemoryStream *stream = g_ptr_array_index (op->self->streams, i);
      g_autoptr(GError) error = NULL;
      guint64 enabled;
      guint64 running;

      if (!sysprof_perf_event_stream_disable (stream->stream, &error))
        memory_diagnostic (op->recording,
                           "CPU %d: stop/flush failed: %s",
                           stream->cpu,
                           error->message);

      if (_sysprof_perf_event_stream_read_times (stream->stream, &enabled, &running))
        {
          g_key_file_set_uint64 (stream->description, "coverage", "time-enabled", enabled);
          g_key_file_set_uint64 (stream->description, "coverage", "time-running", running);

          if (running == 0 || running < enabled)
            memory_diagnostic (op->recording,
                               "CPU %d: PMU ran for %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT
                               " ns; samples do not describe complete coverage",
                               stream->cpu,
                               running,
                               enabled);
        }
      else
        {
          memory_diagnostic (op->recording,
                             "CPU %d: final PMU scheduling coverage is unavailable",
                             stream->cpu);
        }

      g_key_file_set_uint64 (stream->description, "coverage", "samples", stream->samples);
      g_key_file_set_uint64 (stream->description, "coverage", "lost", stream->lost);
      g_key_file_set_uint64 (stream->description, "coverage", "malformed", stream->malformed);
      g_key_file_set_uint64 (stream->description, "coverage", "throttles", stream->throttles);
      write_description (stream, op->recording);

      if (stream->lost ||
          stream->malformed ||
          stream->throttles)
        memory_diagnostic (op->recording,
                           "CPU %d: lost=%" G_GUINT64_FORMAT ", malformed=%" G_GUINT64_FORMAT
                           ", throttles=%" G_GUINT64_FORMAT,
                           stream->cpu,
                           stream->lost,
                           stream->malformed,
                           stream->throttles);
    }

  g_ptr_array_set_size (op->self->streams, 0);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_memory_sampler_prepare (SysprofInstrument *instrument,
                                SysprofRecording  *recording)
{
  Operation *op = g_new0 (Operation, 1);

  g_assert (SYSPROF_IS_MEMORY_SAMPLER (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  op->self = g_object_ref (SYSPROF_MEMORY_SAMPLER (instrument));
  op->recording = g_object_ref (recording);

  return dex_scheduler_spawn (NULL,
                              0,
                              prepare_fiber,
                              op,
                              (GDestroyNotify)operation_free);
}

static DexFuture *
sysprof_memory_sampler_record (SysprofInstrument *instrument,
                               SysprofRecording  *recording,
                               GCancellable      *cancellable)
{
  Operation *op = g_new0 (Operation, 1);

  g_assert (SYSPROF_IS_MEMORY_SAMPLER (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  op->self = g_object_ref (SYSPROF_MEMORY_SAMPLER (instrument));
  op->recording = g_object_ref (recording);
  op->cancel = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              record_fiber,
                              op,
                              (GDestroyNotify)operation_free);
}

static char **
sysprof_memory_sampler_list_required_policy (SysprofInstrument *instrument)
{
  const char *policy[] = { "org.gnome.sysprof3.profile", NULL };

  return g_strdupv ((char **)policy);
}

static void
sysprof_memory_sampler_finalize (GObject *object)
{
  SysprofMemorySampler *self = SYSPROF_MEMORY_SAMPLER (object);

  g_clear_pointer (&self->streams, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_memory_sampler_parent_class)->finalize (object);
}

static void
sysprof_memory_sampler_class_init (SysprofMemorySamplerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_memory_sampler_finalize;

  instrument_class->list_required_policy = sysprof_memory_sampler_list_required_policy;
  instrument_class->prepare = sysprof_memory_sampler_prepare;
  instrument_class->record = sysprof_memory_sampler_record;
}

static void
sysprof_memory_sampler_init (SysprofMemorySampler *self)
{
  self->streams = g_ptr_array_new_with_free_func (memory_stream_free);
}

/**
 * sysprof_memory_sampler_new:
 *
 * Creates an opt-in Intel load-latency sampler. Samples user-space accesses
 * with a period of 1000 and a latency threshold of 30 cycles, where supported.
 * Stacks are perf callchains collected at delivery, without user-space unwinding.
 * Coverage and failures are reported as recording diagnostics and event metadata.
 *
 * Returns: (transfer full): a new instrument
 * Since: 52
 */
SysprofInstrument *
sysprof_memory_sampler_new (void)
{
  return g_object_new (SYSPROF_TYPE_MEMORY_SAMPLER, NULL);
}
