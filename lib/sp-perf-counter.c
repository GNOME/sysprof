/* sp-perf-counter.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef ENABLE_SYSPROFD
# include <polkit/polkit.h>
#endif

#include "sp-perf-counter.h"

#include "util.h"

/*
 * Number of pages to map for the ring buffer.  We map one additional buffer
 * at the beginning for header information to communicate with perf.
 */
#define N_PAGES 32

/*
 * This represents a stream coming to us from perf. All SpPerfCounterInfo
 * share a single GSource used for watching incoming G_IO_IN requests.
 * The map is the mmap() zone we are using as a ring buffer for communicating
 * with perf. The rest is for managing the ring buffer.
 */
typedef struct
{
  gint                         fd;
  gpointer                     fdtag;
  struct perf_event_mmap_page *map;
  guint8                      *data;
  guint64                      tail;
  gint                         cpu;
  guint                        in_callback : 1;
} SpPerfCounterInfo;

struct _SpPerfCounter
{
  volatile gint ref_count;

  /*
   * If we are should currently be enabled. We allow calling
   * multiple times and disabling when we reach zero.
   */
  guint enabled;

  /*
   * Our main context and source for delivering callbacks.
   */
  GMainContext *context;
  GSource *source;

  /*
   * An array of SpPerfCounterInfo, indicating all of our open
   * perf stream.s
   */
  GPtrArray *info;

  /*
   * The callback to execute for every discovered perf event.
   */
  SpPerfCounterCallback callback;
  gpointer callback_data;
  GDestroyNotify callback_data_destroy;

  /*
   * The number of samples we've recorded.
   */
  guint64 n_samples;
};

typedef struct
{
  GSource        source;
  SpPerfCounter *counter;
} PerfGSource;

G_DEFINE_BOXED_TYPE (SpPerfCounter,
                     sp_perf_counter,
                     (GBoxedCopyFunc)sp_perf_counter_ref,
                     (GBoxedFreeFunc)sp_perf_counter_unref)

#ifdef ENABLE_SYSPROFD
static GDBusConnection *shared_conn;
#endif

static gboolean
perf_gsource_dispatch (GSource     *source,
                       GSourceFunc  callback,
                       gpointer     user_data)
{
  return callback ? callback (user_data) : G_SOURCE_CONTINUE;
}

static GSourceFuncs source_funcs = {
  NULL, NULL, perf_gsource_dispatch, NULL
};

static void
sp_perf_counter_info_free (SpPerfCounterInfo *info)
{
  if (info->map)
    {
      gsize map_size;

      map_size = N_PAGES * getpagesize () + getpagesize ();
      munmap (info->map, map_size);

      info->map = NULL;
      info->data = NULL;
    }

  if (info->fd != -1)
    {
      close (info->fd);
      info->fd = 0;
    }

  g_slice_free (SpPerfCounterInfo, info);
}

static void
sp_perf_counter_finalize (SpPerfCounter *self)
{
  guint i;

  g_assert (self != NULL);
  g_assert (self->ref_count == 0);

  for (i = 0; i < self->info->len; i++)
    {
      SpPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      if (info->fdtag)
        g_source_remove_unix_fd (self->source, info->fdtag);

      sp_perf_counter_info_free (info);
    }

  if (self->callback_data_destroy)
    self->callback_data_destroy (self->callback_data);

  g_clear_pointer (&self->source, g_source_destroy);
  g_clear_pointer (&self->info, g_ptr_array_free);
  g_clear_pointer (&self->context, g_main_context_unref);
  g_slice_free (SpPerfCounter, self);
}

void
sp_perf_counter_unref (SpPerfCounter *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sp_perf_counter_finalize (self);
}

SpPerfCounter *
sp_perf_counter_ref (SpPerfCounter *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

static void
sp_perf_counter_flush (SpPerfCounter     *self,
                       SpPerfCounterInfo *info)
{
  guint64 head;
  guint64 tail;
  guint64 n_bytes = N_PAGES * getpagesize ();
  guint64 mask = n_bytes - 1;

  g_assert (self != NULL);
  g_assert (info != NULL);

  tail = info->tail;
  head = info->map->data_head;

  read_barrier ();

  if (head < tail)
    tail = head;

  while ((head - tail) >= sizeof (struct perf_event_header))
    {
      g_autofree guint8 *free_me = NULL;
      struct perf_event_header *header;
      guint8 buffer[4096];

      /* Note that:
       *
       * - perf events are a multiple of 64 bits
       * - the perf event header is 64 bits
       * - the data area is a multiple of 64 bits
       *
       * which means there will always be space for one header, which means we
       * can safely dereference the size field.
       */
      header = (struct perf_event_header *)(gpointer)(info->data + (tail & mask));

      if (header->size > head - tail)
        {
          /* The kernel did not generate a complete event.
           * I don't think that can happen, but we may as well
           * be paranoid.
           */
          break;
        }

      if (info->data + (tail & mask) + header->size > info->data + n_bytes)
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

          memcpy (b, info->data + (tail & mask), n_before);
          memcpy (b + n_before, info->data, n_after);

          header = (struct perf_event_header *)(gpointer)b;
        }

      if (header->type == PERF_RECORD_SAMPLE)
        self->n_samples++;

      if (self->callback != NULL)
        {
          info->in_callback = TRUE;
          self->callback ((SpPerfCounterEvent *)header, info->cpu, self->callback_data);
          info->in_callback = FALSE;
        }

      tail += header->size;
    }

  info->tail = tail;
  info->map->data_tail = tail;
}

static gboolean
sp_perf_counter_dispatch (gpointer user_data)
{
  SpPerfCounter *self = user_data;
  guint i;

  g_assert (self != NULL);
  g_assert (self->info != NULL);

  for (i = 0; i < self->info->len; i++)
    {
      SpPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      sp_perf_counter_flush (self, info);
    }

  return G_SOURCE_CONTINUE;
}

static void
sp_perf_counter_enable_info (SpPerfCounter     *self,
                             SpPerfCounterInfo *info)
{
  g_assert (self != NULL);
  g_assert (info != NULL);

  if (0 != ioctl (info->fd, PERF_EVENT_IOC_ENABLE))
    g_warning ("Failed to enable counters");

  g_source_modify_unix_fd (self->source, info->fdtag, G_IO_IN);
}

SpPerfCounter *
sp_perf_counter_new (GMainContext *context)
{
  SpPerfCounter *ret;

  if (context == NULL)
    context = g_main_context_default ();

  ret = g_slice_new0 (SpPerfCounter);
  ret->ref_count = 1;
  ret->info = g_ptr_array_new ();
  ret->context = g_main_context_ref (context);
  ret->source = g_source_new (&source_funcs, sizeof (PerfGSource));

  ((PerfGSource *)ret->source)->counter = ret;
  g_source_set_callback (ret->source, sp_perf_counter_dispatch, ret, NULL);
  g_source_set_name (ret->source, "[perf]");
  g_source_attach (ret->source, context);

  return ret;
}

void
sp_perf_counter_close (SpPerfCounter *self,
                       gint           fd)
{
  guint i;

  g_return_if_fail (self != NULL);
  g_return_if_fail (fd != -1);

  for (i = 0; i < self->info->len; i++)
    {
      SpPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      if (info->fd == fd)
        {
          g_ptr_array_remove_index (self->info, i);
          if (self->source)
            g_source_remove_unix_fd (self->source, info->fdtag);
          sp_perf_counter_info_free (info);
          return;
        }
    }
}

static void
sp_perf_counter_add_info (SpPerfCounter *self,
                          int            fd,
                          int            cpu)
{
  SpPerfCounterInfo *info;
  guint8 *map;
  gsize map_size;

  g_assert (self != NULL);
  g_assert (fd != -1);

  map_size = N_PAGES * getpagesize () + getpagesize ();
  map = mmap (NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (map == MAP_FAILED)
    {
      close (fd);
      return;
    }

  info = g_slice_new0 (SpPerfCounterInfo);
  info->fd = fd;
  info->map = (gpointer)map;
  info->data = map + getpagesize ();
  info->tail = 0;
  info->cpu = cpu;

  g_ptr_array_add (self->info, info);

  info->fdtag = g_source_add_unix_fd (self->source, info->fd, G_IO_ERR);

  if (self->enabled)
    sp_perf_counter_enable_info (self, info);
}

void
sp_perf_counter_take_fd (SpPerfCounter *self,
                         int            fd)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (fd > -1);

  sp_perf_counter_add_info (self, fd, -1);
}

#ifdef ENABLE_SYSPROFD
static GDBusProxy *
get_proxy (void)
{
  static GDBusProxy *proxy;

  if (proxy != NULL)
    return g_object_ref (proxy);

  if (shared_conn == NULL)
    shared_conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);

  if (shared_conn == NULL)
    return NULL;

  proxy = g_dbus_proxy_new_sync (shared_conn,
                                 (G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                  G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION),
                                 NULL,
                                 "org.gnome.Sysprof2",
                                 "/org/gnome/Sysprof2",
                                 "org.gnome.Sysprof2",
                                 NULL, NULL);

  if (proxy != NULL)
    {
      g_object_add_weak_pointer (G_OBJECT (proxy), (gpointer *)&proxy);
      return g_object_ref (proxy);
    }

  return NULL;
}

static gboolean
authorize_proxy (GDBusProxy *proxy)
{
  PolkitSubject *subject = NULL;
  GPermission *permission = NULL;
  GDBusConnection *conn;
  const gchar *name;

  g_assert (G_IS_DBUS_PROXY (proxy));

  conn = g_dbus_proxy_get_connection (proxy);
  if (conn == NULL)
    goto failure;

  name = g_dbus_connection_get_unique_name (conn);
  if (name == NULL)
    goto failure;

  subject = polkit_system_bus_name_new (name);
  if (subject == NULL)
    goto failure;

  permission = polkit_permission_new_sync ("org.gnome.sysprof2.perf-event-open", subject, NULL, NULL);
  if (permission == NULL)
    goto failure;

  if (!g_permission_acquire (permission, NULL, NULL))
    goto failure;

  return TRUE;

failure:
  g_clear_object (&subject);
  g_clear_object (&permission);

  return FALSE;
}

static GDBusProxy *
get_authorized_proxy (void)
{
  g_autoptr(GDBusProxy) proxy = NULL;

  proxy = get_proxy ();
  if (proxy != NULL && authorize_proxy (proxy))
    return g_steal_pointer (&proxy);

  return NULL;
}

static void
sp_perf_counter_ping_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GDBusProxy *proxy = (GDBusProxy *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) ret = NULL;
  GError *error = NULL;

  g_assert (G_IS_DBUS_PROXY (proxy));
  g_assert (G_IS_TASK (task));
  g_assert (G_IS_ASYNC_RESULT (result));

  ret = g_dbus_proxy_call_finish (proxy, result, &error);

  if (error != NULL)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

static void
sp_perf_counter_acquire_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GPermission *permission = (GPermission *)object;
  g_autoptr(GDBusProxy) proxy = NULL;
  GError *error = NULL;

  g_assert (G_IS_PERMISSION (permission));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_permission_acquire_finish (permission, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  proxy = get_proxy ();

  if (proxy == NULL)
    {
      /* We don't connect at startup, shouldn't happen */
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to create proxy");
      return;
    }

  g_dbus_proxy_call (proxy,
                     "org.freedesktop.DBus.Peer.Ping",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     5000,
                     g_task_get_cancellable (task),
                     sp_perf_counter_ping_cb,
                     g_object_ref (task));
}

static void
sp_perf_counter_permission_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GPermission *permission;
  GError *error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (NULL == (permission = polkit_permission_new_finish (result, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  g_permission_acquire_async (permission,
                              g_task_get_cancellable (task),
                              sp_perf_counter_acquire_cb,
                              g_object_ref (task));

  g_object_unref (permission);
}

static void
sp_perf_counter_get_bus_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GTask) task = user_data;
  PolkitSubject *subject = NULL;
  const gchar *name;
  GError *error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (NULL == (bus = g_bus_get_finish (result, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  shared_conn = g_object_ref (bus);

  name = g_dbus_connection_get_unique_name (bus);
  subject = polkit_system_bus_name_new (name);

  polkit_permission_new ("org.gnome.sysprof2.perf-event-open",
                         subject,
                         g_task_get_cancellable (task),
                         sp_perf_counter_permission_cb,
                         g_object_ref (task));

  g_object_unref (subject);
}

#endif

void
sp_perf_counter_authorize_async (GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (NULL, cancellable, callback, user_data);

#ifdef ENABLE_SYSPROFD
  g_bus_get (G_BUS_TYPE_SYSTEM,
             cancellable,
             sp_perf_counter_get_bus_cb,
             g_object_ref (task));
#else
  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Sysprofd is not supported in current configuration");
#endif
}

gboolean
sp_perf_counter_authorize_finish (GAsyncResult  *result,
                                  GError       **error)
{
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

gint
sp_perf_counter_open (SpPerfCounter          *self,
                      struct perf_event_attr *attr,
                      GPid                    pid,
                      gint                    cpu,
                      gint                    group_fd,
                      gulong                  flags)
{
#ifdef ENABLE_SYSPROFD
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GUnixFDList) fdlist = NULL;
  g_autoptr(GVariant) res = NULL;
  g_autoptr(GVariant) params = NULL;
  gint handle = -1;
#endif
  gint ret = -1;

  g_return_val_if_fail (self != NULL, -1);
  g_return_val_if_fail (attr != NULL, -1);

  /*
   * First, we try to run the syscall locally, since we should avoid the
   * polkit request unless we have to use it for elevated privileges.
   */
  if (-1 != (ret = syscall (__NR_perf_event_open, attr, pid, cpu, group_fd, flags)))
    {
      sp_perf_counter_take_fd (self, ret);
      return ret;
    }

#ifdef ENABLE_SYSPROFD
  params = g_variant_new_parsed (
			"("
				"["
					"{'comm', <%b>},"
#ifdef HAVE_PERF_CLOCKID
					"{'clockid', <%i>},"
					"{'use_clockid', <%b>},"
#endif
					"{'config', <%t>},"
					"{'disabled', <%b>},"
					"{'exclude_idle', <%b>},"
					"{'mmap', <%b>},"
					"{'wakeup_events', <%u>},"
					"{'sample_id_all', <%b>},"
					"{'sample_period', <%t>},"
					"{'sample_type', <%t>},"
					"{'task', <%b>},"
					"{'type', <%u>}"
				"],"
				"%i,"
				"%i,"
				"%t"
			")",
      (gboolean)!!attr->comm,
#ifdef HAVE_PERF_CLOCKID
      (gint32)attr->clockid,
      (gboolean)!!attr->use_clockid,
#endif
      (guint64)attr->config,
      (gboolean)!!attr->disabled,
      (gboolean)!!attr->exclude_idle,
      (gboolean)!!attr->mmap,
      (guint32)attr->wakeup_events,
      (gboolean)!!attr->sample_id_all,
      (guint64)attr->sample_period,
      (guint64)attr->sample_type,
      (gboolean)!!attr->task,
      (guint32)attr->type,
      (gint32)pid,
      (gint32)cpu,
      (guint64)flags);

  params = g_variant_ref_sink (params);

  if (NULL == (proxy = get_authorized_proxy ()))
    {
      errno = EPERM;
      return -1;
    }

  res = g_dbus_proxy_call_with_unix_fd_list_sync (proxy,
                                                  "PerfEventOpen",
                                                  params,
                                                  G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                  60000,
                                                  NULL,
                                                  &fdlist,
                                                  NULL,
                                                  &error);

  if (res == NULL)
    {
      g_autofree gchar *str = g_variant_print (params, TRUE);

      g_warning ("PerfEventOpen: %s: %s", error->message, str);
      return -1;
    }

  if (!g_variant_is_of_type (res, (const GVariantType *)"(h)"))
    {
      g_warning ("Received something other than a handle");
      return -1;
    }

  if (fdlist == NULL)
    {
      g_warning ("Failed to receive fdlist");
      return -1;
    }

  g_variant_get (res, "(h)", &handle);

  if (-1 == (ret = g_unix_fd_list_get (fdlist, handle, &error)))
    {
      g_warning ("%s", error->message);
      return -1;
    }

  sp_perf_counter_take_fd (self, ret);
#endif

  return ret;
}

void
sp_perf_counter_set_callback (SpPerfCounter         *self,
                              SpPerfCounterCallback  callback,
                              gpointer               callback_data,
                              GDestroyNotify         callback_data_destroy)
{
  g_return_if_fail (self != NULL);

  if (self->callback_data_destroy)
    self->callback_data_destroy (self->callback_data);

  self->callback = callback;
  self->callback_data = callback_data;
  self->callback_data_destroy = callback_data_destroy;
}

void
sp_perf_counter_enable (SpPerfCounter *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_int_add (&self->enabled, 1) == 0)
    {
      guint i;

      for (i = 0; i < self->info->len; i++)
        {
          SpPerfCounterInfo *info = g_ptr_array_index (self->info, i);

          sp_perf_counter_enable_info (self, info);
        }
    }
}

void
sp_perf_counter_disable (SpPerfCounter *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_int_dec_and_test (&self->enabled))
    {
      guint i;

      for (i = 0; i < self->info->len; i++)
        {
          SpPerfCounterInfo *info = g_ptr_array_index (self->info, i);

          if (0 != ioctl (info->fd, PERF_EVENT_IOC_DISABLE))
            g_warning ("Failed to disable counters");

          if (!info->in_callback)
            sp_perf_counter_flush (self, info);

          g_source_modify_unix_fd (self->source, info->fdtag, G_IO_ERR);
        }
    }
}
