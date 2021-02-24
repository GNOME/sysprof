/* sysprof-helpers.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-helpers"

#include "config.h"

#include <gio/gunixfdlist.h>

#include "sysprof-helpers.h"
#include "sysprof-polkit-private.h"

#include "helpers.h"
#include "ipc-service.h"

struct _SysprofHelpers
{
  GObject     parent_instance;
  IpcService *proxy;
  GQueue      auth_tasks;
  guint       did_auth : 1;
};

G_DEFINE_TYPE (SysprofHelpers, sysprof_helpers, G_TYPE_OBJECT)

static void
sysprof_helpers_finalize (GObject *object)
{
  SysprofHelpers *self = (SysprofHelpers *)object;

  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (sysprof_helpers_parent_class)->finalize (object);
}

static void
sysprof_helpers_class_init (SysprofHelpersClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_helpers_finalize;
}

static void
sysprof_helpers_init (SysprofHelpers *self)
{
  g_autoptr(GDBusConnection) bus = NULL;

  if ((bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL)))
    self->proxy = ipc_service_proxy_new_sync (bus,
                                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
                                              "org.gnome.Sysprof3",
                                              "/org/gnome/Sysprof3",
                                              NULL, NULL);
}

SysprofHelpers *
sysprof_helpers_get_default (void)
{
  static SysprofHelpers *instance;

  if (g_once_init_enter (&instance))
    {
      SysprofHelpers *self = g_object_new (SYSPROF_TYPE_HELPERS, NULL);
      g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&instance);
      g_once_init_leave (&instance, self);
    }

  return instance;
}

static gboolean
fail_if_no_proxy (SysprofHelpers *self,
                  GTask          *task)
{
  g_assert (SYSPROF_IS_HELPERS (self));
  g_assert (G_IS_TASK (task));

  if (self->proxy == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_CONNECTED,
                               "No D-Bus proxy to communicate with daemon");
      return TRUE;
    }

  return FALSE;
}

static void
sysprof_helpers_list_processes_local_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gint32 *processes = NULL;
  gsize n_processes = 0;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (helpers_list_processes_finish (result, &processes, &n_processes, &error))
    {
      g_autoptr(GVariant) ret = NULL;

      ret = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                       processes,
                                       n_processes,
                                       sizeof (gint32));
      g_task_return_pointer (task,
                             g_variant_take_ref (g_steal_pointer (&ret)),
                             (GDestroyNotify) g_variant_unref);

      return;
    }

  g_task_return_error (task, g_steal_pointer (&error));
}

static void
sysprof_helpers_list_processes_cb (IpcService   *service,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) processes = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!ipc_service_call_list_processes_finish (service, &processes, result, &error))
    helpers_list_processes_async (g_task_get_cancellable (task),
                                  sysprof_helpers_list_processes_local_cb,
                                  g_object_ref (task));
  else
    g_task_return_pointer (task, g_steal_pointer (&processes), (GDestroyNotify) g_variant_unref);
}

gboolean
sysprof_helpers_list_processes (SysprofHelpers  *self,
                                GCancellable    *cancellable,
                                gint32         **processes,
                                gsize           *n_processes,
                                GError         **error)
{
  g_autoptr(GVariant) fixed_ar = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (processes != NULL, FALSE);
  g_return_val_if_fail (n_processes != NULL, FALSE);

  if (helpers_can_see_pids ())
    {
      /* No need to query remote if we can see pids in this namespace */
      if (helpers_list_processes (processes, n_processes))
        return TRUE;
    }

  if (self->proxy && ipc_service_call_list_processes_sync (self->proxy, &fixed_ar, cancellable, NULL))
    {
      const gint32 *data;
      gsize len;

      data = g_variant_get_fixed_array (fixed_ar, &len, sizeof (gint32));
      *processes = g_memdup2 (data, len * sizeof (gint32));
      *n_processes = len;

      return TRUE;
    }

  helpers_list_processes (processes, n_processes);

  return TRUE;
}

void
sysprof_helpers_list_processes_async (SysprofHelpers      *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_list_processes_async);

  if (self->proxy != NULL)
    ipc_service_call_list_processes (self->proxy,
                                     cancellable,
                                     (GAsyncReadyCallback) sysprof_helpers_list_processes_cb,
                                     g_steal_pointer (&task));
  else
    helpers_list_processes_async (cancellable,
                                  sysprof_helpers_list_processes_local_cb,
                                  g_steal_pointer (&task));
}

gboolean
sysprof_helpers_list_processes_finish (SysprofHelpers  *self,
                                       GAsyncResult    *result,
                                       gint32         **processes,
                                       gsize           *n_processes,
                                       GError         **error)
{
  g_autoptr(GVariant) ret = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((ret = g_task_propagate_pointer (G_TASK (result), error)))
    {
      const gint32 *p;
      gsize n;

      p = g_variant_get_fixed_array (ret, &n, sizeof (gint32));

      if (processes != NULL)
        *processes = g_memdup2 (p, n * sizeof (gint32));

      if (n_processes != NULL)
        *n_processes = n;

      return TRUE;
    }

  return FALSE;
}

gboolean
sysprof_helpers_get_proc_fd (SysprofHelpers  *self,
                             const gchar     *path,
                             GCancellable    *cancellable,
                             gint            *out_fd,
                             GError         **error)
{
  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (out_fd != NULL, FALSE);

  *out_fd = -1;

  if (self->proxy != NULL)
    {
      g_autoptr(GVariant) reply = NULL;
      g_autoptr(GUnixFDList) out_fd_list = NULL;

      reply = g_dbus_proxy_call_with_unix_fd_list_sync (G_DBUS_PROXY (self->proxy),
                                                        "GetProcFd",
                                                        g_variant_new ("(^ay)", path),
                                                        G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                        -1,
                                                        NULL,
                                                        &out_fd_list,
                                                        cancellable,
                                                        error);

      if (reply != NULL && out_fd_list != NULL)
        {
          gint handle = -1;

          g_variant_get (reply, "(h)", &handle);

          if (handle < g_unix_fd_list_get_length (out_fd_list))
            {
              *out_fd = g_unix_fd_list_get (out_fd_list, handle, error);
              return *out_fd != -1;
            }
        }
    }

  if (!helpers_get_proc_fd (path, out_fd))
    return FALSE;

  g_clear_error (error);
  return TRUE;
}

static void
sysprof_helpers_get_proc_file_cb (IpcService   *service,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *contents = NULL;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!ipc_service_call_get_proc_file_finish (service, &contents, result, &error))
    {
      const gchar *path = g_task_get_task_data (task);
      gsize len;

      if (!helpers_get_proc_file (path, &contents, &len))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      g_clear_error (&error);
    }

  g_task_return_pointer (task, g_steal_pointer (&contents), g_free);
}

gboolean
sysprof_helpers_get_proc_file (SysprofHelpers  *self,
                               const gchar     *path,
                               GCancellable    *cancellable,
                               gchar          **contents,
                               GError         **error)
{
  gsize len;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);

  if (self->proxy != NULL)
    {
      if (ipc_service_call_get_proc_file_sync (self->proxy, path, contents, cancellable, error))
        return TRUE;
    }

  if (!helpers_get_proc_file (path, contents, &len))
    return FALSE;

  if (error != NULL)
    g_clear_error (error);

  return TRUE;
}

void
sysprof_helpers_get_proc_file_async (SysprofHelpers      *self,
                                     const gchar         *path,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_get_proc_file_async);
  g_task_set_task_data (task, g_strdup (path), g_free);

  if (!fail_if_no_proxy (self, task))
    ipc_service_call_get_proc_file (self->proxy,
                                    path,
                                    cancellable,
                                    (GAsyncReadyCallback) sysprof_helpers_get_proc_file_cb,
                                    g_steal_pointer (&task));
}

gboolean
sysprof_helpers_get_proc_file_finish (SysprofHelpers  *self,
                                      GAsyncResult    *result,
                                      gchar          **contents,
                                      GError         **error)
{
  g_autofree gchar *ret = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((ret = g_task_propagate_pointer (G_TASK (result), error)))
    {
      if (contents != NULL)
        *contents = g_steal_pointer (&ret);
      return TRUE;
    }

  return FALSE;
}

#ifdef __linux__
static GVariant *
build_options_dict (struct perf_event_attr *attr)
{
  return g_variant_take_ref (
    g_variant_new_parsed ("["
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
                          "]",
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
                          (guint32)attr->type));
}

gboolean
sysprof_helpers_perf_event_open (SysprofHelpers          *self,
                                 struct perf_event_attr  *attr,
                                 gint32                   pid,
                                 gint32                   cpu,
                                 gint32                   group_fd,
                                 guint64                  flags,
                                 GCancellable            *cancellable,
                                 gint                    *out_fd,
                                 GError                 **error)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GUnixFDList) out_fd_list = NULL;
  g_autoptr(GVariant) options = NULL;
  g_autoptr(GVariant) reply = NULL;
  gint handle = -1;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (group_fd >= -1, FALSE);
  g_return_val_if_fail (out_fd != NULL, FALSE);

  *out_fd = -1;

  if (self->proxy == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_CONNECTED,
                   "No access to system proxy");
      return FALSE;
    }

  if (group_fd != -1)
    {
      fd_list = g_unix_fd_list_new ();
      handle = g_unix_fd_list_append (fd_list, group_fd, NULL);
    }

  options = build_options_dict (attr);

  reply = g_dbus_proxy_call_with_unix_fd_list_sync (G_DBUS_PROXY (self->proxy),
                                                    "PerfEventOpen",
                                                    g_variant_new ("(@a{sv}iiht)",
                                                                   options,
                                                                   pid,
                                                                   cpu,
                                                                   handle,
                                                                   flags),
                                                    G_DBUS_CALL_FLAGS_NONE,
                                                    -1,
                                                    fd_list,
                                                    &out_fd_list,
                                                    cancellable,
                                                    error);

  if (reply == NULL)
    {
      /* Try in-process (without elevated privs) */
      if (helpers_perf_event_open (options, pid, cpu, group_fd, flags, out_fd))
        {
          g_clear_error (error);
          return TRUE;
        }

      return FALSE;
    }

  if (out_fd_list == NULL || g_unix_fd_list_get_length (out_fd_list) != 1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Received invalid reply from peer");
      return FALSE;
    }

  *out_fd = g_unix_fd_list_get (out_fd_list, 0, error);

  return *out_fd != -1;
}
#endif /* __linux__ */

static void
sysprof_helpers_authorize_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  g_autoptr(SysprofHelpers) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_HELPERS (self));

  if (!_sysprof_polkit_authorize_for_bus_finish (result, &error))
    {
      while (self->auth_tasks.length > 0)
        {
          g_autoptr(GTask) task = g_queue_pop_head (&self->auth_tasks);
          g_task_return_error (task, g_error_copy (error));
        }
    }
  else
    {
      self->did_auth = TRUE;
      while (self->auth_tasks.length > 0)
        {
          g_autoptr(GTask) task = g_queue_pop_head (&self->auth_tasks);
          g_task_return_boolean (task, TRUE);
        }
    }
}

static void
sysprof_helpers_do_auth (SysprofHelpers *self)
{
  GDBusConnection *bus;

  g_assert (SYSPROF_IS_HELPERS (self));

  if (self->proxy == NULL || self->did_auth)
    {
      /* No D-Bus/Polkit? Bail early, fail sooner. If we already successfully
       * did auth, then short circuit to avoid spamming the user.
       */
      while (self->auth_tasks.length > 0)
        {
          g_autoptr(GTask) task = g_queue_pop_head (&self->auth_tasks);
          g_task_return_boolean (task, TRUE);
        }

      return;
    }

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (self->proxy));

  _sysprof_polkit_authorize_for_bus_async (bus,
                                           "org.gnome.sysprof3.profile",
                                           NULL,
                                           TRUE,
                                           NULL,
                                           sysprof_helpers_authorize_cb,
                                           g_object_ref (self));
}

void
sysprof_helpers_authorize_async (SysprofHelpers      *self,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_authorize_async);

  g_queue_push_tail (&self->auth_tasks, g_steal_pointer (&task));

  if (self->auth_tasks.length == 1)
    sysprof_helpers_do_auth (self);
}

gboolean
sysprof_helpers_authorize_finish (SysprofHelpers  *self,
                                  GAsyncResult    *result,
                                  GError         **error)
{
  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
sysprof_helpers_get_process_info (SysprofHelpers  *self,
                                  const gchar     *attributes,
                                  gboolean         no_proxy,
                                  GCancellable    *cancellable,
                                  GVariant       **info,
                                  GError         **error)
{
  g_assert (SYSPROF_IS_HELPERS (self));
  g_assert (attributes != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (info != NULL);

  if (no_proxy)
    {
      *info = helpers_get_process_info (attributes);
      return TRUE;
    }

  return ipc_service_call_get_process_info_sync (self->proxy, attributes, info, cancellable, error);
}

static void
sysprof_helpers_get_process_info_cb (IpcService   *service,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) info = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!ipc_service_call_get_process_info_finish (service, &info, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_steal_pointer (&info), (GDestroyNotify)g_variant_unref);
}

void
sysprof_helpers_get_process_info_async (SysprofHelpers      *self,
                                        const gchar         *attributes,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_HELPERS (self));
  g_assert (attributes != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_get_process_info_async);

  ipc_service_call_get_process_info (self->proxy,
                                     attributes,
                                     cancellable,
                                     (GAsyncReadyCallback) sysprof_helpers_get_process_info_cb,
                                     g_steal_pointer (&task));
}

gboolean
sysprof_helpers_get_process_info_finish (SysprofHelpers  *self,
                                         GAsyncResult    *result,
                                         GVariant       **info,
                                         GError         **error)
{
  g_autoptr(GVariant) ret = NULL;

  g_assert (SYSPROF_IS_HELPERS (self));
  g_assert (G_IS_TASK (result));

  if ((ret = g_task_propagate_pointer (G_TASK (result), error)))
    {
      if (info)
        *info = g_steal_pointer (&ret);
      return TRUE;
    }

  return FALSE;
}

static void
sysprof_helpers_set_governor_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  IpcService *proxy = (IpcService *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  gchar *old_governor = NULL;

  g_assert (IPC_IS_SERVICE (proxy));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!ipc_service_call_set_governor_finish (proxy, &old_governor, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, old_governor, g_free);
}

void
sysprof_helpers_set_governor_async (SysprofHelpers      *self,
                                    const gchar         *governor,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));
  g_return_if_fail (governor != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_set_governor_async);

  if (fail_if_no_proxy (self, task))
    return;

  ipc_service_call_set_governor (self->proxy,
                                 governor,
                                 cancellable,
                                 sysprof_helpers_set_governor_cb,
                                 g_steal_pointer (&task));
}

gboolean
sysprof_helpers_set_governor_finish (SysprofHelpers  *self,
                                     GAsyncResult    *result,
                                     gchar          **old_governor,
                                     GError         **error)
{
  g_autofree gchar *ret = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((ret = g_task_propagate_pointer (G_TASK (result), error)))
    {
      if (old_governor != NULL)
        *old_governor = g_steal_pointer (&ret);
      return TRUE;
    }

  return FALSE;
}

static void
sysprof_helpers_set_paranoid_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  IpcService *proxy = (IpcService *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  int old_paranoid = G_MAXINT;

  g_assert (IPC_IS_SERVICE (proxy));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!ipc_service_call_set_paranoid_finish (proxy, &old_paranoid, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_int (task, old_paranoid);
}

void
sysprof_helpers_set_paranoid_async (SysprofHelpers      *self,
                                    int                  paranoid,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_set_paranoid_async);

  if (fail_if_no_proxy (self, task))
    return;

  ipc_service_call_set_paranoid (self->proxy,
                                 paranoid,
                                 cancellable,
                                 sysprof_helpers_set_paranoid_cb,
                                 g_steal_pointer (&task));
}

gboolean
sysprof_helpers_set_paranoid_finish (SysprofHelpers  *self,
                                     GAsyncResult    *result,
                                     int             *old_paranoid,
                                     GError         **error)
{
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  *old_paranoid = g_task_propagate_int (G_TASK (result), &local_error);

  if (local_error)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}
