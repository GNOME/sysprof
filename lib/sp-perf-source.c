/* sp-perf-source.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <errno.h>
#include <string.h>

#include "sp-clock.h"
#include "sp-perf-counter.h"
#include "sp-perf-source.h"

#define N_WAKEUP_EVENTS 149

struct _SpPerfSource
{
  GObject          parent_instance;

  SpCaptureWriter *writer;
  SpPerfCounter   *counter;
  GHashTable      *pids;

  guint            running : 1;
  guint            is_ready : 1;
};

static void source_iface_init (SpSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpPerfSource, sp_perf_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SOURCE, source_iface_init))

enum {
  TARGET_EXITED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sp_perf_source_real_target_exited (SpPerfSource *self)
{
  g_assert (SP_IS_PERF_SOURCE (self));

  sp_source_emit_finished (SP_SOURCE (self));
}

static void
sp_perf_source_finalize (GObject *object)
{
  SpPerfSource *self = (SpPerfSource *)object;

  g_clear_pointer (&self->writer, sp_capture_writer_unref);
  g_clear_pointer (&self->counter, sp_perf_counter_unref);
  g_clear_pointer (&self->pids, g_hash_table_unref);

  G_OBJECT_CLASS (sp_perf_source_parent_class)->finalize (object);
}

static void
sp_perf_source_class_init (SpPerfSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_perf_source_finalize;

  signals [TARGET_EXITED] =
    g_signal_new_class_handler ("target-exited",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (sp_perf_source_real_target_exited),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
sp_perf_source_init (SpPerfSource *self)
{
  self->pids = g_hash_table_new (NULL, NULL);
}

static gboolean
do_emit_exited (gpointer data)
{
  g_autoptr(SpPerfSource) self = data;

  g_signal_emit (self, signals [TARGET_EXITED], 0);

  return G_SOURCE_REMOVE;
}

static void
sp_perf_source_handle_sample (SpPerfSource                   *self,
                              gint                            cpu,
                              const SpPerfCounterEventSample *sample)
{
  const guint64 *ips;
  gint n_ips;
  guint64 trace[3];

  g_assert (SP_IS_PERF_SOURCE (self));
  g_assert (sample != NULL);

  ips = sample->ips;
  n_ips = sample->n_ips;

  if (n_ips == 0)
    {
      if (sample->header.misc & PERF_RECORD_MISC_KERNEL)
        {
          trace[0] = PERF_CONTEXT_KERNEL;
          trace[1] = sample->ip;
          trace[2] = PERF_CONTEXT_USER;

          ips = trace;
          n_ips = 3;
        }
      else
        {
          trace[0] = PERF_CONTEXT_USER;
          trace[1] = sample->ip;

          ips = trace;
          n_ips = 2;
        }
    }

  sp_capture_writer_add_sample (self->writer,
                                sample->time,
                                cpu,
                                sample->pid,
                                ips,
                                n_ips);
}

static void
sp_perf_source_handle_event (SpPerfCounterEvent *event,
                             guint               cpu,
                             gpointer            user_data)
{
  SpPerfSource *self = user_data;
  SpPerfCounterSuffix *suffix;

  g_assert (SP_IS_PERF_SOURCE (self));
  g_assert (event != NULL);

  switch (event->header.type)
    {
    case PERF_RECORD_COMM:
      suffix = (SpPerfCounterSuffix *)event->raw
             + G_STRUCT_OFFSET (SpPerfCounterEventComm, comm)
             + strlen (event->comm.comm)
             + 1;

      sp_capture_writer_add_process (self->writer,
                                     suffix->time,
                                     cpu,
                                     event->comm.pid,
                                     event->comm.comm);

      break;

    case PERF_RECORD_EXIT:
      sp_capture_writer_add_exit (self->writer,
                                  event->exit.time,
                                  cpu,
                                  event->exit.pid);

      if (g_hash_table_contains (self->pids, GINT_TO_POINTER (event->exit.pid)))
        {
          g_hash_table_remove (self->pids, GINT_TO_POINTER (event->exit.pid));

          if (self->running && (g_hash_table_size (self->pids) > 0))
            {
              self->running = FALSE;
              sp_perf_counter_disable (self->counter);
              g_timeout_add (0, do_emit_exited, g_object_ref (self));
            }
        }

      break;

    case PERF_RECORD_FORK:
      sp_capture_writer_add_fork (self->writer,
                                  event->fork.time,
                                  cpu,
                                  event->fork.ppid,
                                  event->fork.pid);

      /*
       * TODO: We should add support for "follow fork" of the GPid if we are
       *       targetting it.
       */

      break;

    case PERF_RECORD_LOST:
      break;

    case PERF_RECORD_MMAP:
      suffix = (SpPerfCounterSuffix *)event->raw
             + G_STRUCT_OFFSET (SpPerfCounterEventMmap, filename)
             + strlen (event->mmap.filename)
             + 1;

      sp_capture_writer_add_map (self->writer,
                                 suffix->time,
                                 cpu,
                                 event->mmap.pid,
                                 event->mmap.addr,
                                 event->mmap.addr + event->mmap.len,
                                 event->mmap.pgoff,
                                 0,
                                 event->mmap.filename);

      break;

    case PERF_RECORD_READ:
      break;

    case PERF_RECORD_SAMPLE:
      sp_perf_source_handle_sample (self, cpu, &event->sample);
      break;

    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    default:
      break;
    }
}

static gboolean
sp_perf_source_start_pid (SpPerfSource  *self,
                          GPid           pid,
                          GError       **error)
{
  struct perf_event_attr attr = { 0 };
  gulong flags = 0;
  gint ncpu = g_get_num_processors ();
  gint cpu = 0;
  gint fd;

  g_assert (SP_IS_PERF_SOURCE (self));

  attr.sample_type = PERF_SAMPLE_IP
                   | PERF_SAMPLE_TID
                   | PERF_SAMPLE_CALLCHAIN
                   | PERF_SAMPLE_TIME;
  attr.wakeup_events = N_WAKEUP_EVENTS;
  attr.disabled = TRUE;
  attr.mmap = 1;
  attr.comm = 1;
  attr.task = 1;
  attr.exclude_idle = 1;
  attr.size = sizeof attr;
  attr.clockid = sp_clock;
  attr.use_clockid = 1;

  if (pid != -1)
    {
      ncpu = 0;
      cpu = -1;
    }

  for (; cpu < ncpu; cpu++)
    {
      attr.type = PERF_TYPE_HARDWARE;
      attr.config = PERF_COUNT_HW_CPU_CYCLES;
      attr.sample_period = 1200000;

      fd = sp_perf_counter_open (self->counter, &attr, pid, cpu, -1, flags);

      if (fd == -1)
        {
          /*
           * We might just not have access to hardware counters, so try to
           * gracefully fallback to software counters.
           */
          attr.type = PERF_TYPE_SOFTWARE;
          attr.config = PERF_COUNT_SW_CPU_CLOCK;
          attr.sample_period = 1000000;

          errno = 0;

          fd = sp_perf_counter_open (self->counter, &attr, pid, cpu, -1, flags);

          if (fd == -1)
            {
              if (errno == EPERM || errno == EACCES)
                g_set_error (error,
                             G_IO_ERROR,
                             G_IO_ERROR_PERMISSION_DENIED,
                             _("Sysprof requires authorization to access your computers performance counters."));
              else
                g_set_error (error,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             _("An error occurred while attempting to access performance counters: %s"),
                             g_strerror (errno));

              sp_source_stop (SP_SOURCE (self));

              return FALSE;
            }
        }
    }

  return TRUE;
}

static void
sp_perf_source_start (SpSource *source)
{
  SpPerfSource *self = (SpPerfSource *)source;
  g_autoptr(GError) error = NULL;

  g_assert (SP_IS_PERF_SOURCE (self));

  self->counter = sp_perf_counter_new (NULL);

  sp_perf_counter_set_callback (self->counter,
                                sp_perf_source_handle_event,
                                self, NULL);

  if (g_hash_table_size (self->pids) > 0)
    {
      GHashTableIter iter;
      gpointer key;

      g_hash_table_iter_init (&iter, self->pids);

      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          GPid pid = GPOINTER_TO_INT (key);

          if (!sp_perf_source_start_pid (self, pid, &error))
            {
              sp_source_emit_failed (source, error);
              return;
            }
        }
    }
  else
    {
      if (!sp_perf_source_start_pid (self, -1, &error))
        {
          sp_source_emit_failed (source, error);
          return;
        }
    }

  self->running = TRUE;

  sp_perf_counter_enable (self->counter);

  sp_source_emit_ready (source);
}

static void
sp_perf_source_stop (SpSource *source)
{
  SpPerfSource *self = (SpPerfSource *)source;

  g_assert (SP_IS_PERF_SOURCE (self));

  if (self->running)
    {
      self->running = FALSE;
      sp_perf_counter_disable (self->counter);
    }

  g_clear_pointer (&self->counter, sp_perf_counter_unref);

  sp_source_emit_finished (source);
}

static void
sp_perf_source_set_writer (SpSource        *source,
                           SpCaptureWriter *writer)
{
  SpPerfSource *self = (SpPerfSource *)source;

  g_assert (SP_IS_PERF_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sp_capture_writer_ref (writer);
}

static void
sp_perf_source_add_pid (SpSource *source,
                        GPid      pid)
{
  SpPerfSource *self = (SpPerfSource *)source;

  g_return_if_fail (SP_IS_PERF_SOURCE (self));
  g_return_if_fail (pid >= -1);
  g_return_if_fail (self->writer == NULL);

  g_hash_table_add (self->pids, GINT_TO_POINTER (pid));
}

static void
sp_perf_source_authorize_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(SpPerfSource) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));

  if (!sp_perf_counter_authorize_finish (result, &error))
    {
      sp_source_emit_failed (SP_SOURCE (self), error);
      return;
    }

  self->is_ready = TRUE;

  sp_source_emit_ready (SP_SOURCE (self));
}

static void
sp_perf_source_prepare (SpSource *source)
{
  SpPerfSource *self = (SpPerfSource *)source;

  g_assert (SP_IS_PERF_SOURCE (self));

  sp_perf_counter_authorize_async (NULL,
                                   sp_perf_source_authorize_cb,
                                   g_object_ref (self));
}

static gboolean
sp_perf_source_get_is_ready (SpSource *source)
{
  SpPerfSource *self = (SpPerfSource *)source;

  g_assert (SP_IS_PERF_SOURCE (self));

  return self->is_ready;
}

static void
source_iface_init (SpSourceInterface *iface)
{
  iface->start = sp_perf_source_start;
  iface->stop = sp_perf_source_stop;
  iface->set_writer = sp_perf_source_set_writer;
  iface->add_pid = sp_perf_source_add_pid;
  iface->prepare = sp_perf_source_prepare;
  iface->get_is_ready = sp_perf_source_get_is_ready;
}

SpSource *
sp_perf_source_new (void)
{
  return g_object_new (SP_TYPE_PERF_SOURCE, NULL);
}

void
sp_perf_source_set_target_pid (SpPerfSource *self,
                               GPid          pid)
{
  g_return_if_fail (SP_IS_PERF_SOURCE (self));
  g_return_if_fail (pid >= -1);

  if (pid == -1)
    g_hash_table_remove_all (self->pids);
  else
    sp_perf_source_add_pid (SP_SOURCE (self), pid);
}
