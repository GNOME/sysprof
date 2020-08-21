/* sysprof-perf-source.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "sysprof-clock.h"
#include "sysprof-helpers.h"
#include "sysprof-line-reader.h"
#include "sysprof-perf-counter.h"
#include "sysprof-perf-source.h"

#define N_WAKEUP_EVENTS 149

/* Identifiers for the various tracepoints we might watch for */
enum SysprofTracepoint
{
  DRM_VBLANK,
  DRM_I915_BEGIN,
  DRM_I915_END,
};

typedef struct
{
  enum SysprofTracepoint tp;
  const char       *path;
  const char      **fields;
} SysprofOptionalTracepoint;

/* Global list of the optional tracepoints we might want to watch. */
static const SysprofOptionalTracepoint optional_tracepoints[] = {
  /* This event fires just after the vblank IRQ handler starts.
   *
   * Note that on many platforms when nothing is waiting for vblank
   * (no pageflips have happened recently, no rendering is
   * synchronizing to vblank), the vblank IRQ will get masked off and
   * the event won't show up in the timeline.
   *
   * Also note that when we're in watch-a-single-process mode, we
   * won't get the event since it comes in on an IRQ handler, not for
   * our pid.
   */
  { DRM_VBLANK, "drm/drm_vblank_event",
    (const char *[]){ "crtc", "seq", NULL } },

  /* I915 GPU execution.
   *
   * These are the wrong events to be watching.  We need to use the
   * ones under CONFIG_DRM_I915_LOW_LEVEL_TRACEPOINTS instead.
   */
#if 0
  { DRM_I915_BEGIN, "i915/i915_gem_request_add",
    (const char *[]){ "ctx", "ring", "seqno", NULL } },
  { DRM_I915_END, "i915/i915_gem_request_retire",
    (const char *[]){ "ctx", "ring", "seqno", NULL } },
#endif
};

/* Struct describing tracepoint events.
 *
 * This should be extended with some sort of union for the describing
 * the locations of the relevant fields within the _RAW section of the
 * struct perf_event, so we can pick out things like the vblank CRTC
 * number and MSC.
 */
typedef struct {
  enum SysprofTracepoint tp;
  gsize field_offsets[0];
} SysprofTracepointDesc;

struct _SysprofPerfSource
{
  GObject               parent_instance;

  SysprofCaptureWriter *writer;
  SysprofPerfCounter   *counter;
  GHashTable           *pids;

  /* Mapping from perf sample identifiers to SysprofTracepointDesc. */
  GHashTable           *tracepoint_event_ids;

  guint                 running : 1;
  guint                 is_ready : 1;
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofPerfSource, sysprof_perf_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

enum {
  TARGET_EXITED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sysprof_perf_source_real_target_exited (SysprofPerfSource *self)
{
  g_assert (SYSPROF_IS_PERF_SOURCE (self));

  sysprof_source_emit_finished (SYSPROF_SOURCE (self));
}

static void
sysprof_perf_source_finalize (GObject *object)
{
  SysprofPerfSource *self = (SysprofPerfSource *)object;

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->counter, sysprof_perf_counter_unref);
  g_clear_pointer (&self->pids, g_hash_table_unref);
  g_clear_pointer (&self->tracepoint_event_ids, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_perf_source_parent_class)->finalize (object);
}

static void
sysprof_perf_source_class_init (SysprofPerfSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_perf_source_finalize;

  signals [TARGET_EXITED] =
    g_signal_new_class_handler ("target-exited",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (sysprof_perf_source_real_target_exited),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
sysprof_perf_source_init (SysprofPerfSource *self)
{
  self->pids = g_hash_table_new (NULL, NULL);
  self->tracepoint_event_ids = g_hash_table_new (NULL, NULL);
}

static gboolean
do_emit_exited (gpointer data)
{
  g_autoptr(SysprofPerfSource) self = data;

  g_signal_emit (self, signals [TARGET_EXITED], 0);

  return G_SOURCE_REMOVE;
}

static void
sysprof_perf_source_handle_tracepoint (SysprofPerfSource                       *self,
                                       gint                                     cpu,
                                       const SysprofPerfCounterEventTracepoint *sample,
                                       SysprofTracepointDesc                   *tp_desc)
{
  gchar *message = NULL;

  /* Note that field_offsets[] correspond to the
   * SysprofOptionalTracepoint->fields[] strings.  Yes, this is gross.
   */
  switch (tp_desc->tp)
    {
    case DRM_VBLANK:
      message = g_strdup_printf ("crtc=%d, seq=%u",
                                 *(gint *)(gpointer)(sample->raw + tp_desc->field_offsets[0]),
                                 *(guint *)(gpointer)(sample->raw + tp_desc->field_offsets[1]));

      sysprof_capture_writer_add_mark (self->writer,
                                       sample->time,
                                       cpu,
                                       sample->pid,
                                       0,
                                       "drm",
                                       "vblank",
                                       message);
      break;

    case DRM_I915_BEGIN:
    case DRM_I915_END:
      message = g_strdup_printf ("ctx=%u, ring=%u, seqno=%u",
                                 *(guint *)(gpointer)(sample->raw + tp_desc->field_offsets[0]),
                                 *(guint *)(gpointer)(sample->raw + tp_desc->field_offsets[1]),
                                 *(guint *)(gpointer)(sample->raw + tp_desc->field_offsets[2]));

      sysprof_capture_writer_add_mark (self->writer,
                                       sample->time,
                                       cpu,
                                       sample->pid,
                                       0,
                                       "drm",
                                       (tp_desc->tp == DRM_I915_BEGIN ?
                                        "i915 gpu begin" : "i915 gpu end"),
                                       message);
      break;

    default:
      break;
    }

  g_free (message);
}

static void
sysprof_perf_source_handle_callchain (SysprofPerfSource                      *self,
                                      gint                                    cpu,
                                      const SysprofPerfCounterEventCallchain *sample)
{
  const guint64 *ips;
  gint n_ips;
  guint64 trace[3];

  g_assert (SYSPROF_IS_PERF_SOURCE (self));
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

  sysprof_capture_writer_add_sample (self->writer,
                                sample->time,
                                cpu,
                                sample->pid,
                                sample->tid,
                                ips,
                                n_ips);
}

static inline void
realign (gsize *pos,
         gsize  align)
{
  *pos = (*pos + align - 1) & ~(align - 1);
}

static void
sysprof_perf_source_handle_event (SysprofPerfCounterEvent *event,
                                  guint                    cpu,
                                  gpointer                 user_data)
{
  SysprofPerfSource *self = user_data;
  SysprofTracepointDesc *tp_desc;
  gsize offset;
  gint64 time;

  g_assert (SYSPROF_IS_PERF_SOURCE (self));
  g_assert (event != NULL);

  switch (event->header.type)
    {
    case PERF_RECORD_COMM:
      offset = strlen (event->comm.comm) + 1;
      realign (&offset, sizeof (guint64));
      offset += sizeof (GPid) + sizeof (GPid);
      memcpy (&time, event->comm.comm + offset, sizeof time);

      sysprof_capture_writer_add_process (self->writer,
                                          time,
                                          cpu,
                                          event->comm.pid,
                                          event->comm.comm);

      break;

    case PERF_RECORD_EXIT:
      /* Ignore fork exits for now */
      if (event->exit.tid != event->exit.pid)
        break;

      sysprof_capture_writer_add_exit (self->writer,
                                       event->exit.time,
                                       cpu,
                                       event->exit.pid);

      if (g_hash_table_contains (self->pids, GINT_TO_POINTER (event->exit.pid)))
        {
          g_hash_table_remove (self->pids, GINT_TO_POINTER (event->exit.pid));

          if (self->running && (g_hash_table_size (self->pids) == 0))
            {
              self->running = FALSE;
              sysprof_perf_counter_disable (self->counter);
              g_timeout_add (0, do_emit_exited, g_object_ref (self));
            }
        }

      break;

    case PERF_RECORD_FORK:
      sysprof_capture_writer_add_fork (self->writer,
                                       event->fork.time,
                                       cpu,
                                       event->fork.ptid,
                                       event->fork.tid);

      /*
       * TODO: We should add support for "follow fork" of the GPid if we are
       *       targetting it.
       */

      break;

    case PERF_RECORD_LOST:
      break;

    case PERF_RECORD_MMAP:
      offset = strlen (event->mmap.filename) + 1;
      realign (&offset, sizeof (guint64));
      offset += sizeof (GPid) + sizeof (GPid);
      memcpy (&time, event->mmap.filename + offset, sizeof time);

      sysprof_capture_writer_add_map (self->writer,
                                      time,
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
      /* We don't capture IPs with tracepoints, and get _RAW data
       * instead.  Handle them separately.
       */
      g_assert (&event->callchain.identifier == &event->tracepoint.identifier);
      tp_desc = g_hash_table_lookup (self->tracepoint_event_ids,
                                     GINT_TO_POINTER (event->callchain.identifier));
      if (tp_desc)
        {
          sysprof_perf_source_handle_tracepoint (self, cpu, &event->tracepoint, tp_desc);
        }
      else
        {
          sysprof_perf_source_handle_callchain (self, cpu, &event->callchain);
        }
      break;

    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    default:
      break;
    }
}

static gboolean
sysprof_perf_get_tracepoint_config (const char *path,
                                    gint64     *config)
{
  g_autofree gchar *filename = NULL;
  g_autofree gchar *contents = NULL;
  gsize len;

  filename = g_strdup_printf ("/sys/kernel/debug/tracing/events/%s/id", path);
  if (!g_file_get_contents (filename, &contents, &len, NULL))
    return FALSE;

  *config = g_ascii_strtoull (contents, NULL, 10);

  return TRUE;
}

static gboolean
sysprof_perf_get_tracepoint_fields (SysprofTracepointDesc            *tp_desc,
                                    const SysprofOptionalTracepoint  *optional_tp,
                                    GError                          **error)
{
  gchar *filename = NULL;
  gchar *contents;
  size_t len;
  gint i;
  filename = g_strdup_printf ("/sys/kernel/debug/tracing/events/%s/format",
                              optional_tp->path);
  if (!filename)
    return FALSE;

  if (!g_file_get_contents (filename, &contents, &len, NULL))
    {
      g_free (filename);
      return FALSE;
    }

  g_free (filename);

  /* Look up our fields.  Some example strings:
   *
   *  field:unsigned short common_type;	offset:0;	size:2;	signed:0;
   *	field:int crtc;	offset:8;	size:4;	signed:1;
   *  field:unsigned int seq;	offset:12;	size:4;	signed:0;
   */
  for (i = 0; optional_tp->fields[i] != NULL; i++)
    {
      gchar *pattern = g_strdup_printf ("%s;\toffset:", optional_tp->fields[i]);
      gchar *match;
      gint64 offset;

      match = strstr (contents, pattern);
      if (!match)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       _("Sysprof failed to find field “%s”."),
                       optional_tp->fields[i]);
          g_free (contents);
          return FALSE;
        }

      offset = g_ascii_strtoll (match + strlen (pattern),
                                NULL, 0);
      if (offset == G_MININT64 && errno != 0)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       _("Sysprof failed to parse offset for “%s”."),
                       optional_tp->fields[i]);
          g_free (contents);
          return FALSE;
        }

      tp_desc->field_offsets[i] = offset;
      g_free (pattern);
    }

  g_free (contents);

  return TRUE;
}

/* Adds a perf tracepoint event, if it's available.
 *
 * These are kernel tracepoints that we want to include in our capture
 * when present, but may be kernel version or driver-specific.
 */
static void
sysprof_perf_source_add_optional_tracepoint (SysprofPerfSource                *self,
                                             GPid                              pid,
                                             gint                              cpu,
                                             const SysprofOptionalTracepoint  *optional_tracepoint,
                                             GError                          **error)
{
  struct perf_event_attr attr = { 0 };
  SysprofTracepointDesc *tp_desc;
  gulong flags = 0;
  gint fd;
  gint64 config;
  gint64 id;
  int ret;
  gint num_fields;

  if (!sysprof_perf_get_tracepoint_config(optional_tracepoint->path, &config))
    return;

  attr.type = PERF_TYPE_TRACEPOINT;
  attr.sample_type = PERF_SAMPLE_RAW
                   | PERF_SAMPLE_IP
                   | PERF_SAMPLE_TID
                   | PERF_SAMPLE_IDENTIFIER
                   | PERF_SAMPLE_RAW
                   | PERF_SAMPLE_TIME;
  attr.config = config;
  attr.sample_period = 1;

#ifdef HAVE_PERF_CLOCKID
  attr.clockid = sysprof_clock;
  attr.use_clockid = 1;
#endif

  attr.size = sizeof attr;

  fd = sysprof_perf_counter_open (self->counter, &attr, pid, cpu, -1, flags);

  ret = ioctl (fd, PERF_EVENT_IOC_ID, &id);
  if (ret != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   _("Sysprof failed to get perf_event ID."));
      close(fd);
      return;
    }

  /* The fields list is NULL-terminated, count how many are there. */
  for (num_fields = 0; optional_tracepoint->fields[num_fields]; num_fields++)
    ;

  tp_desc = g_malloc (sizeof (*tp_desc) +
                      sizeof(*tp_desc->field_offsets) * num_fields);
  if (!tp_desc)
    {
      close(fd);
      return;
    }

  tp_desc->tp = optional_tracepoint->tp;

  if (!sysprof_perf_get_tracepoint_fields (tp_desc, optional_tracepoint, error))
    {
      free(tp_desc);
      close(fd);
      return;
    }

  /* Here's where we should inspect the /format file to determine how
   * to pick fields out of the _RAW data.
   */

  /* We're truncating the event ID from 64b to 32 to fit in the hash.
   * The event IDs start from 0 at boot, so meh.
   */
  g_assert (id <= 0xffffffff);
  g_hash_table_insert (self->tracepoint_event_ids, GINT_TO_POINTER (id), tp_desc);
}

static gboolean
sysprof_perf_source_start_pid (SysprofPerfSource  *self,
                               GPid                pid,
                               GError            **error)
{
  struct perf_event_attr attr = { 0 };
  gulong flags = 0;
  gint ncpu = g_get_num_processors ();
  gint cpu = 0;
  gint fd = -1;

  g_assert (SYSPROF_IS_PERF_SOURCE (self));

  attr.sample_type = PERF_SAMPLE_IP
                   | PERF_SAMPLE_TID
                   | PERF_SAMPLE_IDENTIFIER
                   | PERF_SAMPLE_CALLCHAIN
                   | PERF_SAMPLE_TIME;
  attr.wakeup_events = N_WAKEUP_EVENTS;
  attr.disabled = TRUE;
  attr.mmap = 1;
  attr.comm = 1;
  attr.task = 1;
  attr.exclude_idle = 1;
  attr.sample_id_all = 1;

#ifdef HAVE_PERF_CLOCKID
  attr.clockid = sysprof_clock;
  attr.use_clockid = 1;
#endif

  attr.size = sizeof attr;

  if (pid != -1)
    {
      ncpu = 0;
      cpu = -1;
    }

  /* Perf won't let us capture on all CPUs on all pids, so we have to
   * loop over CPUs if we're not just watching a single pid.
   */
  for (; cpu < ncpu; cpu++)
    {
      attr.type = PERF_TYPE_HARDWARE;
      attr.config = PERF_COUNT_HW_CPU_CYCLES;
      attr.sample_period = 1200000;

      fd = sysprof_perf_counter_open (self->counter, &attr, pid, cpu, -1, flags);

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

          fd = sysprof_perf_counter_open (self->counter, &attr, pid, cpu, -1, flags);

          if (fd == -1)
            {
              g_set_error (error,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           _("An error occurred while attempting to access performance counters"));

              sysprof_source_stop (SYSPROF_SOURCE (self));

              return FALSE;
            }
        }

      for (guint i = 0; i < G_N_ELEMENTS(optional_tracepoints); i++)
        sysprof_perf_source_add_optional_tracepoint (self, pid, cpu,
                                                     &optional_tracepoints[i],
                                                     error);
    }

  return TRUE;
}

static void
sysprof_perf_source_start (SysprofSource *source)
{
  SysprofPerfSource *self = (SysprofPerfSource *)source;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_PERF_SOURCE (self));

  self->counter = sysprof_perf_counter_new (NULL);

  sysprof_perf_counter_set_callback (self->counter,
                                     sysprof_perf_source_handle_event,
                                     self, NULL);

  if (g_hash_table_size (self->pids) > 0)
    {
      GHashTableIter iter;
      gpointer key;

      g_hash_table_iter_init (&iter, self->pids);

      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          GPid pid = GPOINTER_TO_INT (key);

          if (!sysprof_perf_source_start_pid (self, pid, &error))
            {
              sysprof_source_emit_failed (source, error);
              return;
            }
        }
    }
  else
    {
      if (!sysprof_perf_source_start_pid (self, -1, &error))
        {
          sysprof_source_emit_failed (source, error);
          return;
        }
    }

  self->running = TRUE;

  sysprof_perf_counter_enable (self->counter);
}

static void
sysprof_perf_source_stop (SysprofSource *source)
{
  SysprofPerfSource *self = (SysprofPerfSource *)source;

  g_assert (SYSPROF_IS_PERF_SOURCE (self));

  if (self->running)
    {
      self->running = FALSE;
      sysprof_perf_counter_disable (self->counter);
    }

  g_clear_pointer (&self->counter, sysprof_perf_counter_unref);

  sysprof_source_emit_finished (source);
}

static void
sysprof_perf_source_set_writer (SysprofSource        *source,
                                SysprofCaptureWriter *writer)
{
  SysprofPerfSource *self = (SysprofPerfSource *)source;

  g_assert (SYSPROF_IS_PERF_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sysprof_capture_writer_ref (writer);
}

static void
sysprof_perf_source_add_pid (SysprofSource *source,
                             GPid      pid)
{
  SysprofPerfSource *self = (SysprofPerfSource *)source;

  g_return_if_fail (SYSPROF_IS_PERF_SOURCE (self));
  g_return_if_fail (pid >= -1);
  g_return_if_fail (self->writer == NULL);

  g_hash_table_add (self->pids, GINT_TO_POINTER (pid));
}

static void
sysprof_perf_source_auth_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofPerfSource) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_PERF_SOURCE (self));

  if (!sysprof_helpers_authorize_finish (helpers, result, &error))
    {
      sysprof_source_emit_failed (SYSPROF_SOURCE (self), error);
    }
  else
    {
      self->is_ready = TRUE;
      sysprof_source_emit_ready (SYSPROF_SOURCE (self));
    }
}

static void
sysprof_perf_source_prepare (SysprofSource *source)
{
  g_assert (SYSPROF_IS_PERF_SOURCE (source));

  sysprof_helpers_authorize_async (sysprof_helpers_get_default (),
                                   NULL,
                                   sysprof_perf_source_auth_cb,
                                   g_object_ref (source));
}

static gboolean
sysprof_perf_source_get_is_ready (SysprofSource *source)
{
  return SYSPROF_PERF_SOURCE (source)->is_ready;
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->start = sysprof_perf_source_start;
  iface->stop = sysprof_perf_source_stop;
  iface->set_writer = sysprof_perf_source_set_writer;
  iface->add_pid = sysprof_perf_source_add_pid;
  iface->prepare = sysprof_perf_source_prepare;
  iface->get_is_ready = sysprof_perf_source_get_is_ready;
}

SysprofSource *
sysprof_perf_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_PERF_SOURCE, NULL);
}

void
sysprof_perf_source_set_target_pid (SysprofPerfSource *self,
                                    GPid          pid)
{
  g_return_if_fail (SYSPROF_IS_PERF_SOURCE (self));
  g_return_if_fail (pid >= -1);

  if (pid == -1)
    g_hash_table_remove_all (self->pids);
  else
    sysprof_perf_source_add_pid (SYSPROF_SOURCE (self), pid);
}
