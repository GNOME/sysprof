/* sp-hostinfo-source.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

#include "sources/sp-hostinfo-source.h"

struct _SpHostinfoSource
{
  GObject          parent_instance;

  guint            handler;
  gint             n_cpu;

  SpCaptureWriter *writer;
  GArray          *cpu_info;
};

typedef struct
{
  gint    counter_base;
  gdouble total;
  gdouble freq;
  glong   last_user;
  glong   last_idle;
  glong   last_system;
  glong   last_nice;
  glong   last_iowait;
  glong   last_irq;
  glong   last_softirq;
  glong   last_steal;
  glong   last_guest;
  glong   last_guest_nice;
} CpuInfo;

static void source_iface_init (SpSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpHostinfoSource, sp_hostinfo_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SOURCE, source_iface_init))

SpSource *
sp_hostinfo_source_new (void)
{
  return g_object_new (SP_TYPE_HOSTINFO_SOURCE, NULL);
}

static void
poll_cpu (SpHostinfoSource *self)
{
  gchar cpu[64] = { 0 };
  glong user;
  glong sys;
  glong nice;
  glong idle;
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
  gchar *buf = NULL;
  glong total;
  gchar *line;
  gint ret;
  gint id;
  gint i;

  if (g_file_get_contents("/proc/stat", &buf, NULL, NULL))
    {
      line = buf;
      for (i = 0; buf[i]; i++)
        {
          if (buf[i] == '\n') {
            buf[i] = '\0';
            if (g_str_has_prefix(line, "cpu"))
              {
                if (isdigit(line[3]))
                  {
                    CpuInfo *cpu_info;

                    user = nice = sys = idle = id = 0;
                    ret = sscanf (line, "%s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                                  cpu, &user, &nice, &sys, &idle,
                                  &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
                    if (ret != 11)
                      goto next;

                    ret = sscanf(cpu, "cpu%d", &id);

                    if (ret != 1 || id < 0 || id >= self->n_cpu)
                      goto next;

                    cpu_info = &g_array_index (self->cpu_info, CpuInfo, id);

                    user_calc = user - cpu_info->last_user;
                    nice_calc = nice - cpu_info->last_nice;
                    system_calc = sys - cpu_info->last_system;
                    idle_calc = idle - cpu_info->last_idle;
                    iowait_calc = iowait - cpu_info->last_iowait;
                    irq_calc = irq - cpu_info->last_irq;
                    softirq_calc = softirq - cpu_info->last_softirq;
                    steal_calc = steal - cpu_info->last_steal;
                    guest_calc = guest - cpu_info->last_guest;
                    guest_nice_calc = guest_nice - cpu_info->last_guest_nice;

                    total = user_calc + nice_calc + system_calc + idle_calc + iowait_calc + irq_calc + softirq_calc + steal_calc + guest_calc + guest_nice_calc;
                    cpu_info->total = ((total - idle_calc) / (gdouble)total) * 100.0;

                    cpu_info->last_user = user;
                    cpu_info->last_nice = nice;
                    cpu_info->last_idle = idle;
                    cpu_info->last_system = sys;
                    cpu_info->last_iowait = iowait;
                    cpu_info->last_irq = irq;
                    cpu_info->last_softirq = softirq;
                    cpu_info->last_steal = steal;
                    cpu_info->last_guest = guest;
                    cpu_info->last_guest_nice = guest_nice;
                  }
              } else {
                /* CPU info comes first. Skip further lines. */
                break;
              }

          next:
            line = &buf[i + 1];
          }
      }
  }

  g_free (buf);
}

static void
publish_cpu (SpHostinfoSource *self)
{
  SpCaptureCounterValue *counter_values;
  guint *counter_ids;
  gint i;

  counter_ids = alloca (sizeof *counter_ids * self->n_cpu * 2);
  counter_values = alloca (sizeof *counter_values * self->n_cpu * 2);

  for (i = 0; i < self->n_cpu; i++)
    {
      CpuInfo *info = &g_array_index (self->cpu_info, CpuInfo, i);
      SpCaptureCounterValue *value = &counter_values[i*2];
      guint *id = &counter_ids[i*2];

      *id = info->counter_base;
      value->vdbl = info->total;

      id++;
      value++;

      *id = info->counter_base + 1;
      value->vdbl = info->freq;
    }

  sp_capture_writer_set_counters (self->writer,
                                  SP_CAPTURE_CURRENT_TIME,
                                  getpid (),
                                  -1,
                                  counter_ids,
                                  counter_values,
                                  self->n_cpu * 2);
}

static gboolean
collect_hostinfo_cb (gpointer data)
{
  SpHostinfoSource *self = data;

  g_assert (SP_IS_HOSTINFO_SOURCE (self));

  poll_cpu (self);
  publish_cpu (self);

  return G_SOURCE_CONTINUE;
}

static void
sp_hostinfo_source_finalize (GObject *object)
{
  SpHostinfoSource *self = (SpHostinfoSource *)object;

  if (self->handler)
    {
      g_source_remove (self->handler);
      self->handler = 0;
    }

  g_clear_pointer (&self->writer, sp_capture_writer_unref);
  g_clear_pointer (&self->cpu_info, g_array_unref);

  G_OBJECT_CLASS (sp_hostinfo_source_parent_class)->finalize (object);
}

static void
sp_hostinfo_source_class_init (SpHostinfoSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_hostinfo_source_finalize;
}

static void
sp_hostinfo_source_init (SpHostinfoSource *self)
{
  self->cpu_info = g_array_new (FALSE, TRUE, sizeof (CpuInfo));
}

static void
sp_hostinfo_source_set_writer (SpSource        *source,
                               SpCaptureWriter *writer)
{
  SpHostinfoSource *self = (SpHostinfoSource *)source;

  g_assert (SP_IS_HOSTINFO_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&self->writer, sp_capture_writer_unref);
  self->writer = sp_capture_writer_ref (writer);
}

static void
sp_hostinfo_source_start (SpSource *source)
{
  SpHostinfoSource *self = (SpHostinfoSource *)source;

  g_assert (SP_IS_HOSTINFO_SOURCE (self));

  self->handler = g_timeout_add (250, collect_hostinfo_cb, self);
}

static void
sp_hostinfo_source_stop (SpSource *source)
{
  SpHostinfoSource *self = (SpHostinfoSource *)source;

  g_assert (SP_IS_HOSTINFO_SOURCE (self));

  g_source_remove (self->handler);
  self->handler = 0;

  sp_source_emit_finished (SP_SOURCE (self));
}

static void
sp_hostinfo_source_prepare (SpSource *source)
{
  SpHostinfoSource *self = (SpHostinfoSource *)source;
  SpCaptureCounter *counters;
  gint i;

  g_assert (SP_IS_HOSTINFO_SOURCE (self));

  self->n_cpu = g_get_num_processors ();

  g_array_set_size (self->cpu_info, 0);

  counters = alloca (sizeof *counters * self->n_cpu * 2);

  for (i = 0; i < self->n_cpu; i++)
    {
      SpCaptureCounter *ctr = &counters[i*2];
      CpuInfo info = { 0 };

      /*
       * Request 2 counter values.
       * One for CPU and one for Frequency.
       */
      info.counter_base = sp_capture_writer_request_counter (self->writer, 2);

      /*
       * Define counters for capture file.
       */
      ctr->id = info.counter_base;
      ctr->type = SP_CAPTURE_COUNTER_DOUBLE;
      ctr->value.vdbl = 0;
      g_strlcpy (ctr->category, "CPU Percent", sizeof ctr->category);
      g_snprintf (ctr->name, sizeof ctr->name, "Total CPU %d", i);
      g_snprintf (ctr->description, sizeof ctr->description,
                  "Total CPU usage %d", i);

      ctr++;

      ctr->id = info.counter_base + 1;
      ctr->type = SP_CAPTURE_COUNTER_DOUBLE;
      ctr->value.vdbl = 0;
      g_strlcpy (ctr->category, "CPU Frequency", sizeof ctr->category);
      g_snprintf (ctr->name, sizeof ctr->name, "CPU %d", i);
      g_snprintf (ctr->description, sizeof ctr->description,
                  "Frequency of CPU %d", i);

      g_array_append_val (self->cpu_info, info);
    }

  sp_capture_writer_define_counters (self->writer,
                                     SP_CAPTURE_CURRENT_TIME,
                                     getpid (),
                                     -1,
                                     counters,
                                     self->n_cpu * 2);

  sp_source_emit_ready (SP_SOURCE (self));
}

static void
source_iface_init (SpSourceInterface *iface)
{
  iface->set_writer = sp_hostinfo_source_set_writer;
  iface->prepare = sp_hostinfo_source_prepare;
  iface->start = sp_hostinfo_source_start;
  iface->stop = sp_hostinfo_source_stop;
}
