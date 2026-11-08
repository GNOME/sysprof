/* sysprof-perf-event-stream.c
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

#include <errno.h>
#include <linux/perf_event.h>
#ifdef HAVE_STDATOMIC_H
# include <stdatomic.h>
#endif
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "sysprof-service-private.h"
#include "sysprof-perf-event-stream-private.h"

#define MIN_RING_PAGES 16

struct _SysprofPerfEventStream
{
  GObject                      parent_instance;

  GDBusConnection             *connection;

  GSource                     *source;

  struct perf_event_attr       attr;

  int                          self_pid;
  int                          cpu;
  int                          group_fd;
  gulong                       flags;

  SysprofPerfEventCallback     callback;
  gpointer                     callback_data;
  GDestroyNotify               callback_data_destroy;

  int                          perf_fd;

  struct perf_event_mmap_page *map;
  guint8                      *map_data;
  guint64                      tail;
  gsize                        map_size;
  guint                        ring_pages;

  guint                        active : 1;
};

typedef struct _SysprofPerfEventSource
{
  GSource                 source;
  SysprofPerfEventStream *stream;
  gint64                  next_ready_time;
  int                     timeout_msec;
} SysprofPerfEventSource;

enum
{
  PROP_0,
  PROP_ACTIVE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofPerfEventStream, sysprof_perf_event_stream, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static int
_perf_event_open (const struct perf_event_attr *attr,
                  pid_t                         pid,
                  int                           cpu,
                  int                           group_fd,
                  unsigned long                 flags)
{
  return syscall (SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

GVariant *
_sysprof_perf_event_attr_to_variant (const struct perf_event_attr *attr)
{
  return g_variant_take_ref (
    g_variant_new_parsed ("["
                            "{'comm', <%b>},"
#ifdef HAVE_PERF_CLOCKID
                            "{'clockid', <%i>},"
                            "{'use_clockid', <%b>},"
#endif
                            "{'config1', <%t>},"
                            "{'config2', <%t>},"
                            "{'read_format', <%t>},"
                            "{'precise_ip', <%u>},"
                            "{'exclude_user', <%b>},"
                            "{'exclude_kernel', <%b>},"
                            "{'exclude_hv', <%b>},"
                            "{'config', <%t>},"
                            "{'disabled', <%b>},"
                            "{'exclude_idle', <%b>},"
                            "{'mmap', <%b>},"
                            "{'mmap2', <%b>},"
                            "{'build_id', <%b>},"
                            "{'wakeup_events', <%u>},"
                            "{'sample_id_all', <%b>},"
                            "{'sample_period', <%t>},"
                            "{'sample_type', <%t>},"
                            "{'task', <%b>},"
                            "{'sample_stack_user', <%u>},"
                            "{'sample_regs_user', <%t>},"
                            "{'type', <%u>}"
                          "]",
                          (gboolean)!!attr->comm,
#ifdef HAVE_PERF_CLOCKID
                          (gint32)attr->clockid,
                          (gboolean)!!attr->use_clockid,
#endif
                          (guint64)attr->config1,
                          (guint64)attr->config2,
                          (guint64)attr->read_format,
                          (guint32)attr->precise_ip,
                          (gboolean)attr->exclude_user,
                          (gboolean)attr->exclude_kernel,
                          (gboolean)attr->exclude_hv,
                          (guint64)attr->config,
                          (gboolean)!!attr->disabled,
                          (gboolean)!!attr->exclude_idle,
                          (gboolean)!!attr->mmap,
                          (gboolean)!!attr->mmap2,
                          (gboolean)!!attr->build_id,
                          (guint32)attr->wakeup_events,
                          (gboolean)!!attr->sample_id_all,
                          (guint64)attr->sample_period,
                          (guint64)attr->sample_type,
                          (gboolean)!!attr->task,
                          (guint32)attr->sample_stack_user,
                          (guint64)attr->sample_regs_user,
                          (guint32)attr->type));
}

static void
sysprof_perf_event_stream_flush (SysprofPerfEventStream *self)
{
  SysprofPerfEventSource *source = (SysprofPerfEventSource *)self->source;
  guint64 n_bytes = self->ring_pages * sysprof_getpagesize ();
  guint64 mask = n_bytes - 1;
  guint64 head;
  guint64 tail;
  gboolean lost_records = FALSE;
  guint us = 0;
  guint them = 0;

  g_assert (SYSPROF_IS_PERF_EVENT_STREAM (self));

  tail = self->tail;
  head = self->map->data_head;

#ifdef HAVE_STDATOMIC_H
  atomic_thread_fence (memory_order_acquire);
#elif G_GNUC_CHECK_VERSION(3, 0)
  __sync_synchronize ();
#endif

  if (head < tail)
    tail = head;

  while ((head - tail) >= sizeof (struct perf_event_header))
    {
      g_autofree guint8 *free_me = NULL;
      const SysprofPerfEvent *event;
      struct perf_event_header *header;
      guint8 buffer[4096];
      gboolean is_self = FALSE;

      /* Note that:
       *
       * - perf events are a multiple of 64 bits
       * - the perf event header is 64 bits
       * - the data area is a multiple of 64 bits
       *
       * which means there will always be space for one header, which means we
       * can safely dereference the size field.
       */
      header = (struct perf_event_header *)(gpointer)(self->map_data + (tail & mask));

      if (header->size < sizeof *header || header->size > n_bytes)
        {
          /* A corrupt header must not stall the ring or permit an oversized copy. */
          tail = head;
          break;
        }

      if (header->size > head - tail)
        {
          /* The kernel did not generate a complete event.
           * I don't think that can happen, but we may as well
           * be paranoid.
           */
          break;
        }

      if (self->map_data + (tail & mask) + header->size > self->map_data + n_bytes)
        {
          gint n_before;
          gint n_after;
          guint8 *b;

          if (header->size > sizeof buffer)
            free_me = b = g_malloc (header->size);
          else
            b = buffer;

          n_after = (tail & mask) + header->size - n_bytes;
          n_before = header->size - n_after;

          memcpy (b, self->map_data + (tail & mask), n_before);
          memcpy (b + n_before, self->map_data, n_after);

          header = (struct perf_event_header *)(gpointer)b;
        }

      event = (SysprofPerfEvent *)header;

      switch (event->header.type)
        {
        default:
        case PERF_RECORD_COMM:
        case PERF_RECORD_EXIT:
        case PERF_RECORD_FORK:
        case PERF_RECORD_MMAP:
        case PERF_RECORD_MMAP2:
          break;

        case PERF_RECORD_SAMPLE:
          {
            gsize offset = sizeof (struct perf_event_header);
            guint32 pid;

            if (self->attr.sample_type & PERF_SAMPLE_IDENTIFIER)
              offset += sizeof (guint64);

            if (self->attr.sample_type & PERF_SAMPLE_IP)
              offset += sizeof (guint64);

            if ((self->attr.sample_type & PERF_SAMPLE_TID) &&
                header->size >= offset + sizeof pid)
              {
                memcpy (&pid, (const guint8 *)event + offset, sizeof pid);
                is_self = pid == self->self_pid;
              }
          }
          break;

        case PERF_RECORD_READ:
          goto skip_callback;

        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
          break;

        case PERF_RECORD_LOST:
          lost_records = TRUE;
          g_debug ("Lost records from perf");
          break;
        }

      if (self->callback != NULL)
        self->callback (event, self->cpu, self->callback_data);

      us += is_self;
      them += !is_self;

    skip_callback:
      tail += header->size;
    }

  self->tail = tail;

#ifdef HAVE_STDATOMIC_H
  atomic_thread_fence (memory_order_seq_cst);
#elif G_GNUC_CHECK_VERSION(3, 0)
  __sync_synchronize ();
#endif

  self->map->data_tail = tail;

  if (lost_records)
    source->timeout_msec = 1;
  else if (them >= 32)
    source->timeout_msec = 5;
  else if (us + them > 0)
    source->timeout_msec = MIN (25, source->timeout_msec + 5);
  else
    source->timeout_msec = MIN (50, source->timeout_msec + 10);
}

static gboolean
sysprof_perf_event_source_dispatch (GSource     *gsource,
                                    GSourceFunc  callback,
                                    gpointer     user_data)
{
  SysprofPerfEventSource *source = (SysprofPerfEventSource *)gsource;
  SysprofPerfEventStream *self = source->stream;

  if (source->next_ready_time <= g_source_get_time (gsource) &&
      self != NULL &&
      self->active &&
      self->map != NULL &&
      self->tail != self->map->data_head)
    sysprof_perf_event_stream_flush (self);
  else
    source->timeout_msec = MIN (50, source->timeout_msec + 10);

  source->next_ready_time = g_get_monotonic_time () + (source->timeout_msec * 1000);
  g_source_set_ready_time (self->source, source->next_ready_time);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs source_funcs = {
  .dispatch = sysprof_perf_event_source_dispatch,
};

static void
sysprof_perf_event_stream_finalize (GObject *object)
{
  SysprofPerfEventStream *self = (SysprofPerfEventStream *)object;

  if (self->callback_data_destroy)
    {
      self->callback_data_destroy (self->callback_data);
      self->callback_data_destroy = NULL;
      self->callback_data = NULL;
    }

  self->callback = NULL;

  g_clear_object (&self->connection);

  if (self->source != NULL)
    {
      g_source_destroy (self->source);
      g_source_unref (self->source);
      self->source = NULL;
    }

  g_clear_fd (&self->perf_fd, NULL);
  g_clear_fd (&self->group_fd, NULL);

  if (self->map != NULL && self->map_size > 0)
    {
      munmap (self->map, self->map_size);
      self->map = NULL;
      self->map_size = 0;
    }

  G_OBJECT_CLASS (sysprof_perf_event_stream_parent_class)->finalize (object);
}

static void
sysprof_perf_event_stream_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofPerfEventStream *self = SYSPROF_PERF_EVENT_STREAM (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      g_value_set_boolean (value, self->active);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_perf_event_stream_class_init (SysprofPerfEventStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_perf_event_stream_finalize;
  object_class->get_property = sysprof_perf_event_stream_get_property;

  properties[PROP_ACTIVE] =
    g_param_spec_boolean ("active", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_perf_event_stream_init (SysprofPerfEventStream *self)
{
  self->perf_fd = -1;
  self->group_fd = -1;
  self->self_pid = getpid ();
}

static DexFuture *
sysprof_perf_event_stream_new_fiber (gpointer data)
{
  SysprofPerfEventStream *self = data;
  g_autofd int perf_fd = -1;
  gsize map_size;
  guint8 *map;

  g_assert (SYSPROF_IS_PERF_EVENT_STREAM (self));

  if (self->connection != NULL)
    {
      g_autoptr(GUnixFDList) fd_list = NULL;
      g_autoptr(GUnixFDList) out_fd_list = NULL;
      g_autoptr(GVariant) options = _sysprof_perf_event_attr_to_variant (&self->attr);
      g_autoptr(DexFuture) future = NULL;
      g_autoptr(GVariant) reply = NULL;
      g_autoptr(GError) error = NULL;
      int group_fd_handle = -1;
      int handle;

      if (self->attr.precise_ip || self->attr.config1 || self->attr.config2 ||
          self->attr.read_format || self->attr.exclude_user || self->attr.exclude_kernel ||
          self->attr.exclude_hv)
        {
          g_autoptr(GVariant) capabilities = NULL;
          g_autoptr(GVariant) capabilities_reply = NULL;
          gboolean supported = FALSE;

          capabilities_reply = dex_await_variant (dex_dbus_connection_call (self->connection,
                                                                            _sysprof_service_get_name (self->connection),
                                                                            "/org/gnome/Sysprof3",
                                                                            "org.gnome.Sysprof3.Service",
                                                                            "GetCapabilities",
                                                                            NULL,
                                                                            G_VARIANT_TYPE ("(a{sv})"),
                                                                            G_DBUS_CALL_FLAGS_NONE,
                                                                            -1),
                                                  &error);

          if (capabilities_reply != NULL)
            {
              g_variant_get (capabilities_reply, "(@a{sv})", &capabilities);
              g_variant_lookup (capabilities, "perf-event-open-memory", "b", &supported);
            }
          else if (!g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
            {
              return dex_future_new_for_error (g_steal_pointer (&error));
            }

          /* Older daemons silently ignore these attributes. */
          if (!supported)
            return dex_future_new_reject (G_IO_ERROR,
                                          G_IO_ERROR_NOT_SUPPORTED,
                                          "sysprofd does not support the perf attributes required for memory sampling");
        }

      if (self->group_fd > -1)
        {
          fd_list = g_unix_fd_list_new ();
          group_fd_handle = g_unix_fd_list_append (fd_list, self->group_fd, NULL);
        }

      future = dex_dbus_connection_call_with_unix_fd_list (self->connection,
                                                           _sysprof_service_get_name (self->connection),
                                                           "/org/gnome/Sysprof3",
                                                           "org.gnome.Sysprof3.Service",
                                                           "PerfEventOpen",
                                                           g_variant_new ("(@a{sv}iiht)",
                                                                          options,
                                                                          -1,
                                                                          self->cpu,
                                                                          group_fd_handle,
                                                                          self->flags),
                                                           G_VARIANT_TYPE ("(h)"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           G_MAXUINT,
                                                           fd_list);

      if (!dex_await (dex_ref (future), &error))
        {
          /* The future set otherwise hides the D-Bus failure behind a generic error. */
          if (DEX_IS_FUTURE_SET (future))
            {
              DexFuture *reply_future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);

              if (dex_future_is_rejected (reply_future))
                {
                  g_clear_error (&error);
                  dex_future_get_value (reply_future, &error);
                }
            }
          return dex_future_new_for_error (g_steal_pointer (&error));
        }

      g_assert (DEX_IS_FUTURE_SET (future));

      if (!(out_fd_list = dex_await_object (dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 1)), &error)) ||
          !(reply = dex_await_variant (dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0)), &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));

      g_variant_get (reply, "(h)", &handle);

      if (-1 == (perf_fd = g_unix_fd_list_get (out_fd_list, handle, &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }
  else
    {
      perf_fd = _perf_event_open (&self->attr, -1, self->cpu, self->group_fd, self->flags);

      if (perf_fd < 0)
        {
          int errsv = errno;
          return dex_future_new_reject (G_IO_ERROR,
                                        g_io_error_from_errno (errsv),
                                        "%s", g_strerror (errsv));
        }
    }

  g_assert (self->perf_fd == -1);
  g_assert (perf_fd > -1);

  self->perf_fd = g_steal_fd (&perf_fd);

  for (guint pages = self->ring_pages; pages >= MIN_RING_PAGES; pages /= 2)
    {
      map_size = (pages + 1) * sysprof_getpagesize ();
      map = mmap (NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, self->perf_fd, 0);

      if ((gpointer)map != MAP_FAILED)
        {
          self->ring_pages = pages;
          break;
        }

      if (pages == MIN_RING_PAGES)
        {
          int errsv = errno;

          return dex_future_new_reject (G_IO_ERROR,
                                        g_io_error_from_errno (errsv),
                                        "%s", g_strerror (errsv));
        }
    }

  self->map_size = map_size;
  self->map = (gpointer)map;
  self->map_data = map + sysprof_getpagesize ();
  self->tail = 0;

  return dex_future_new_take_object (g_object_ref (self));
}

DexFuture *
sysprof_perf_event_stream_new (GDBusConnection          *connection,
                               struct perf_event_attr   *attr,
                               guint                     ring_pages,
                               int                       cpu,
                               int                       group_fd,
                               guint64                   flags,
                               SysprofPerfEventCallback  callback,
                               gpointer                  callback_data,
                               GDestroyNotify            callback_data_destroy)
{
  SysprofPerfEventSource *source;
  g_autoptr(SysprofPerfEventStream) self = NULL;
  g_autofree char *name = NULL;

  g_return_val_if_fail (!connection || G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (attr != NULL, NULL);
  g_return_val_if_fail (ring_pages >= MIN_RING_PAGES &&
                        (ring_pages & (ring_pages - 1)) == 0, NULL);
  g_return_val_if_fail (cpu > -1, NULL);
  g_return_val_if_fail (group_fd >= -1, NULL);

  self = g_object_new (SYSPROF_TYPE_PERF_EVENT_STREAM, NULL);
  self->connection = connection ? g_object_ref (connection) : NULL;
  self->attr = *attr;
  self->ring_pages = ring_pages;
  self->cpu = cpu;
  self->group_fd = group_fd > -1 ? dup (group_fd) : -1;
  self->flags = flags;
  self->callback = callback;
  self->callback_data = callback_data;
  self->callback_data_destroy = callback_data_destroy;

  source = (SysprofPerfEventSource *)
    g_source_new (&source_funcs, sizeof (SysprofPerfEventSource));
  source->stream = self;
  source->timeout_msec = 5;
  source->next_ready_time = g_get_monotonic_time () + (source->timeout_msec * 1000);
  self->source = (GSource *)source;

  name = g_strdup_printf ("[perf cpu%d]", cpu);

  g_source_set_ready_time (self->source, source->next_ready_time);
  g_source_set_name (self->source, name);
  g_source_attach (self->source, NULL);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_perf_event_stream_new_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

gboolean
sysprof_perf_event_stream_enable (SysprofPerfEventStream  *self,
                                  GError                 **error)
{
  g_return_val_if_fail (SYSPROF_IS_PERF_EVENT_STREAM (self), FALSE);

  if (self->active)
    return TRUE;

  if (0 != ioctl (self->perf_fd, PERF_EVENT_IOC_ENABLE))
    {
      int errsv = errno;

      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errsv),
                           g_strerror (errsv));
      return FALSE;
    }

  self->active = TRUE;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);

  return TRUE;
}

gboolean
sysprof_perf_event_stream_disable (SysprofPerfEventStream  *self,
                                   GError                 **error)
{
  g_return_val_if_fail (SYSPROF_IS_PERF_EVENT_STREAM (self), FALSE);

  if (!self->active)
    return TRUE;

  if (0 != ioctl (self->perf_fd, PERF_EVENT_IOC_DISABLE))
    {
      int errsv = errno;

      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errsv),
                           g_strerror (errsv));
      return FALSE;
    }

  self->active = FALSE;

  sysprof_perf_event_stream_flush (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);

  return TRUE;
}

/* Read final scheduling coverage even if the PMU never delivered a sample. */
gboolean
_sysprof_perf_event_stream_read_times (SysprofPerfEventStream *self,
                                       guint64                *enabled,
                                       guint64                *running)
{
  guint64 values[3];
  ssize_t ret;

  g_return_val_if_fail (SYSPROF_IS_PERF_EVENT_STREAM (self), FALSE);
  g_return_val_if_fail (enabled != NULL, FALSE);
  g_return_val_if_fail (running != NULL, FALSE);

  if (self->attr.read_format != (PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING))
    return FALSE;

  do
    ret = read (self->perf_fd, values, sizeof values);
  while (ret < 0 && errno == EINTR);

  if (ret != sizeof values)
    return FALSE;

  *enabled = values[1];
  *running = values[2];
  return TRUE;
}
