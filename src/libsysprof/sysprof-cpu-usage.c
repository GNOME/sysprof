/* sysprof-cpu-usage.c
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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "line-reader-private.h"

#include "sysprof-cpu-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

#define PROC_STAT_BUF_SIZE (4096*4)

struct _SysprofCpuUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofCpuUsageClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofCpuUsage, sysprof_cpu_usage, SYSPROF_TYPE_INSTRUMENT)

typedef struct _CpuInfo
{
  int    counter_base;
  double total;
  glong  last_user;
  glong  last_idle;
  glong  last_system;
  glong  last_nice;
  glong  last_iowait;
  glong  last_irq;
  glong  last_softirq;
  glong  last_steal;
  glong  last_guest;
  glong  last_guest_nice;
} CpuInfo;

typedef struct _CpuFreq
{
  gint64 max;
  int    stat_fd;
  char   buf[116];
} CpuFreq;

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static void
freq_info_clear (gpointer data)
{
  CpuFreq *cpu_freq = data;
  g_clear_fd (&cpu_freq->stat_fd, NULL);
}

static double
get_cpu_freq (int     stat_fd,
              guint   cpu,
              double  max,
              char   *buf,
              gssize  len)
{
  gint64 val;

  if (stat_fd == -1)
    return .0;

  if (len <= 0)
    return .0;

  buf[len] = 0;
  g_strchug (buf);
  val = g_ascii_strtoll (buf, NULL, 10);

  val = CLAMP (val, .0, max);

  return (double)val / max * 100.;
}

static DexFuture *
sysprof_cpu_usage_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  g_autoptr(GArray) cpu_info = NULL;
  g_autoptr(GArray) freq_info = NULL;
  g_autofd int stat_fd = -1;
  g_autofree char *read_buffer = NULL;
  g_autofree SysprofCaptureCounterValue *values = NULL;
  g_autofree SysprofCaptureCounter *counters = NULL;
  g_autofree guint *ids = NULL;
  SysprofCaptureCounter *counter;
  SysprofCaptureWriter *writer;
  guint n_cpu;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);
  n_cpu = g_get_num_processors ();
  stat_fd = open ("/proc/stat", O_RDONLY|O_CLOEXEC);
  g_unix_set_fd_nonblocking (stat_fd, TRUE, NULL);
  read_buffer = g_malloc (PROC_STAT_BUF_SIZE);

  counters = g_new0 (SysprofCaptureCounter, (n_cpu * 2) + 1);
  ids = g_new0 (guint, (n_cpu * 2) + 1);
  values = g_new0 (SysprofCaptureCounterValue, (n_cpu * 2) + 1);

  cpu_info = g_array_new (FALSE, TRUE, sizeof (CpuInfo));
  g_array_set_size (cpu_info, n_cpu);

  freq_info = g_array_new (FALSE, TRUE, sizeof (CpuFreq));
  g_array_set_clear_func (freq_info, freq_info_clear);

  /* Create counter information for all of our counters that we will need
   * to submit in upcoming parses.
   */
  for (guint i = 0; i < n_cpu; i++)
    {
      g_autofree char *max_path = g_strdup_printf ("/sys/devices/system/cpu/cpu%u/cpufreq/scaling_max_freq", i);
      g_autofree char *cur_path = g_strdup_printf ("/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", i);
      g_autofree char *max_value = NULL;
      CpuFreq cf;

      ids[i*2] = sysprof_capture_writer_request_counter (writer, 1);
      ids[i*2+1] = sysprof_capture_writer_request_counter (writer, 1);

      counter = &counters[i*2];
      counter->id = ids[i*2];
      counter->type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
      counter->value.vdbl = 0;
      g_strlcpy (counter->category, "CPU Percent", sizeof counter->category);
      g_snprintf (counter->name, sizeof counter->name, "Total CPU %d", i);
      g_snprintf (counter->description, sizeof counter->description,
                  "Total CPU usage %d", i);

      counter = &counters[i*2+1];
      counter->id = ids[i*2+1];
      counter->type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
      counter->value.vdbl = 0;
      g_strlcpy (counter->category, "CPU Frequency", sizeof counter->category);
      g_snprintf (counter->name, sizeof counter->name, "CPU %d", i);
      g_snprintf (counter->description, sizeof counter->description,
                  "Frequency of CPU %d", i);

      cf.stat_fd = open (cur_path, O_RDONLY|O_CLOEXEC);
      g_unix_set_fd_nonblocking (cf.stat_fd, TRUE, NULL);
      cf.buf[0] = 0;
      if (g_file_get_contents (max_path, &max_value, NULL, NULL))
        cf.max = g_ascii_strtoll (max_value, NULL, 10);
      else
        cf.max = 0;
      g_array_append_val (freq_info, cf);
    }

  /* Now create our combined counter */
  ids[n_cpu*2] = sysprof_capture_writer_request_counter (writer, 1);
  counter = &counters[n_cpu*2];
  counter->id = ids[n_cpu*2];
  counter->type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
  counter->value.vdbl = 0;
  g_strlcpy (counter->category, "CPU Percent", sizeof counter->category);
  g_snprintf (counter->name, sizeof counter->name, "Combined");
  g_snprintf (counter->description, sizeof counter->description, "Combined CPU usage");

  /* Register all the counters as a group */
  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          counters,
                                          n_cpu * 2 + 1);

  for (;;)
    {
      g_autoptr(GPtrArray) futures = g_ptr_array_new_with_free_func (dex_unref);
      g_autoptr(DexFuture) cpu_future = NULL;
      LineReader reader;
      glong total_usage = 0;
      gsize line_len;
      char *line;

      /* First collect all our reads and then wait for them to finish before
       * parsing in a pass. With io_uring, this lets us coalesce all the lseek
       * and reads into a single set of iops.
       */
      for (guint i = 0; i < n_cpu; i++)
        {
          CpuFreq *cf = &g_array_index (freq_info, CpuFreq, i);

          g_ptr_array_add (futures, dex_aio_read (NULL, cf->stat_fd, cf->buf, sizeof cf->buf - 1, 0));
        }
      cpu_future = dex_aio_read (NULL, stat_fd, read_buffer, PROC_STAT_BUF_SIZE, 0);
      g_ptr_array_add (futures, dex_ref (cpu_future));

      /* Wait for all IO futures as they point at buffers owned by the array */
      if (!dex_await (dex_future_allv ((DexFuture **)futures->pdata, futures->len), NULL))
        break;

      /* Now parse all the contents of the stat files which should be
       * populated in the various files.
       */
      line_reader_init (&reader, read_buffer, dex_await_int64 (dex_ref (cpu_future), NULL));
      while ((line = line_reader_next (&reader, &line_len)))
        {
          CpuInfo *ci;
          char cpu[64];
          glong user;
          glong sys;
          glong nice;
          glong idle;
          int id;
          int ret;
          glong iowait;
          glong irq;
          glong softirq;
          glong steal;
          glong guest;
          glong guest_nice;
          glong user_calc;
          glong system_calc;
          glong nice_calc;
          glong idle_calc;
          glong iowait_calc;
          glong irq_calc;
          glong softirq_calc;
          glong steal_calc;
          glong guest_calc;
          glong guest_nice_calc;
          glong total;

          line[line_len] = 0;

          /* CPU lines come first, short-circuit after */
          if (!g_str_has_prefix (line, "cpu"))
            break;

          /* First line is "cpu ..." */
          if (!g_ascii_isdigit (line[3]))
            continue;

          /* Parse the various counters in order */
          user = nice = sys = idle = id = 0;
          ret = sscanf (line, "%63s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                        cpu, &user, &nice, &sys, &idle,
                        &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
          if (ret != 11)
            continue;

          /* Get the CPU identifier */
          ret = sscanf (cpu, "cpu%d", &id);
          if (ret != 1 || id < 0 || id >= n_cpu)
            continue;

          ci = &g_array_index (cpu_info, CpuInfo, id);

          user_calc = user - ci->last_user;
          nice_calc = nice - ci->last_nice;
          system_calc = sys - ci->last_system;
          idle_calc = idle - ci->last_idle;
          iowait_calc = iowait - ci->last_iowait;
          irq_calc = irq - ci->last_irq;
          softirq_calc = softirq - ci->last_softirq;
          steal_calc = steal - ci->last_steal;
          guest_calc = guest - ci->last_guest;
          guest_nice_calc = guest_nice - ci->last_guest_nice;

          total = user_calc + nice_calc + system_calc + idle_calc + iowait_calc + irq_calc + softirq_calc + steal_calc + guest_calc + guest_nice_calc;
          ci->total = ((total - idle_calc) / (double)total) * 100.;

          ci->last_user = user;
          ci->last_nice = nice;
          ci->last_idle = idle;
          ci->last_system = sys;
          ci->last_iowait = iowait;
          ci->last_irq = irq;
          ci->last_softirq = softirq;
          ci->last_steal = steal;
          ci->last_guest = guest;
          ci->last_guest_nice = guest_nice;
        }

      /* Publish counters to the capture file */
      for (guint i = 0; i < n_cpu; i++)
        {
          const CpuInfo *ci = &g_array_index (cpu_info, CpuInfo, i);
          CpuFreq *cf = &g_array_index (freq_info, CpuFreq, i);
          DexFuture *freq_future = g_ptr_array_index (futures, i);
          gssize len = dex_await_int64 (dex_ref (freq_future), NULL);

          values[i*2].vdbl = ci->total;
          values[i*2+1].vdbl = get_cpu_freq (cf->stat_fd, i, cf->max, cf->buf, len);

          total_usage += ci->total;
        }

      values[n_cpu*2].vdbl = total_usage / (double)n_cpu;
      sysprof_capture_writer_set_counters (writer,
                                           SYSPROF_CAPTURE_CURRENT_TIME,
                                           -1,
                                           -1,
                                           ids,
                                           values,
                                           n_cpu * 2 + 1);

      /* Wait for cancellation or ⅕ second */
      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 5),
                                   NULL),
                 NULL);
      if (dex_future_get_status (record->cancellable) != DEX_FUTURE_STATUS_PENDING)
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_cpu_usage_record (SysprofInstrument *instrument,
                          SysprofRecording  *recording,
                          GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_CPU_USAGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_cpu_usage_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_cpu_usage_class_init (SysprofCpuUsageClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_cpu_usage_record;
}

static void
sysprof_cpu_usage_init (SysprofCpuUsage *self)
{
}

SysprofInstrument *
sysprof_cpu_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_CPU_USAGE, NULL);
}
