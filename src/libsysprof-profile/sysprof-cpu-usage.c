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

#include <glib/gstdio.h>

#include "sysprof-cpu-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofCpuUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofCpuUsageClass
{
  SysprofInstrumentClass parent_class;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCpuUsage, sysprof_cpu_usage, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties [N_PROPS];

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

static DexFuture *
sysprof_cpu_usage_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  SysprofCaptureCounter *counters;
  SysprofCaptureCounter *counter;
  SysprofCaptureWriter *writer;
  g_autofd int stat_fd = -1;
  guint n_cpu;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);
  n_cpu = g_get_num_processors ();
  stat_fd = open ("/proc/stat", O_RDONLY|O_CLOEXEC);
  counters = g_alloca (sizeof *counters * ((n_cpu * 2) + 1));

  /* Create counter information for all of our counters that we will need
   * to submit in upcoming parses.
   */
  for (guint i = 0; i < n_cpu; i++)
    {
      guint counter_base = sysprof_capture_writer_request_counter (writer, 2);

      counter = &counters[i*2];
      counter->id = counter_base;
      counter->type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
      counter->value.vdbl = 0;
      g_strlcpy (counter->category, "CPU Percent", sizeof counter->category);
      g_snprintf (counter->name, sizeof counter->name, "Total CPU %d", i);
      g_snprintf (counter->description, sizeof counter->description,
                  "Total CPU usage %d", i);

      counter = &counters[i*2+1];
      counter->id = counter_base + 1;
      counter->type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
      counter->value.vdbl = 0;
      g_strlcpy (counter->category, "CPU Frequency", sizeof counter->category);
      g_snprintf (counter->name, sizeof counter->name, "CPU %d", i);
      g_snprintf (counter->description, sizeof counter->description,
                  "Frequency of CPU %d", i);
    }

  /* Now create our combined counter */
  counter = &counters[n_cpu*2];
  counter->id = sysprof_capture_writer_request_counter (writer, 1);
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

  g_print ("Registering %d counters\n", n_cpu * 2 + 1);

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
sysprof_cpu_usage_finalize (GObject *object)
{
  SysprofCpuUsage *self = (SysprofCpuUsage *)object;

  G_OBJECT_CLASS (sysprof_cpu_usage_parent_class)->finalize (object);
}

static void
sysprof_cpu_usage_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SysprofCpuUsage *self = SYSPROF_CPU_USAGE (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_usage_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SysprofCpuUsage *self = SYSPROF_CPU_USAGE (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_usage_class_init (SysprofCpuUsageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_cpu_usage_finalize;
  object_class->get_property = sysprof_cpu_usage_get_property;
  object_class->set_property = sysprof_cpu_usage_set_property;

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
