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
#include <polkit/polkit.h>

#include "sysprof-helpers.h"
#include "sysprof-backport-autocleanups.h"

#include "helpers.h"
#include "ipc-service.h"

struct _SysprofHelpers
{
  GObject     parent_instance;
  IpcService *proxy;
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

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  g_return_if_fail (bus != NULL);

  self->proxy = ipc_service_proxy_new_sync (bus,
                                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
                                            "org.gnome.Sysprof3",
                                            "/org/gnome/Sysprof3",
                                            NULL, NULL);
  g_return_if_fail (self->proxy != NULL);
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
    {
      g_autofree gint32 *out_processes = NULL;
      gsize out_n_processes = 0;

      if (helpers_list_processes (&out_processes, &out_n_processes))
        processes = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                               out_processes,
                                               out_n_processes,
                                               sizeof (gint32));
    }

  if (processes != NULL)
    g_task_return_pointer (task, g_steal_pointer (&processes), (GDestroyNotify) g_variant_unref);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_NOT_SUPPORTED,
                             "Failed to list processes");
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

  if (ipc_service_call_list_processes_sync (self->proxy, &fixed_ar, cancellable, NULL))
    {
      const gint32 *data;
      gsize len;

      data = g_variant_get_fixed_array (fixed_ar, &len, sizeof (gint32));
      *processes = g_memdup (data, len * sizeof (gint32));
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

  if (!fail_if_no_proxy (self, task))
    ipc_service_call_list_processes (self->proxy,
                                     cancellable,
                                     (GAsyncReadyCallback) sysprof_helpers_list_processes_cb,
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
        *processes = g_memdup (p, n * sizeof (gint32));

      if (n_processes != NULL)
        *n_processes = n;

      return TRUE;
    }

  return FALSE;
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
                                                    G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
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

gboolean
sysprof_helpers_perf_event_open_finish (SysprofHelpers  *self,
                                        GAsyncResult    *result,
                                        gint            *out_fd,
                                        GError         **error)
{
  g_autoptr(GUnixFDList) fd_list = NULL;

  g_return_val_if_fail (SYSPROF_IS_HELPERS (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((fd_list = g_task_propagate_pointer (G_TASK (result), error)))
    {
      if (g_unix_fd_list_get_length (fd_list) != 1)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "Incorrect number of FDs from peer");
          return FALSE;
        }

      if (out_fd != NULL)
        {
          gint fd = g_unix_fd_list_get (fd_list, 0, error);

          if (fd == -1)
            return FALSE;

          *out_fd = fd;
        }

      return TRUE;
    }

  return FALSE;
}
#endif /* __linux__ */

static void
sysprof_helpers_check_authorization_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  PolkitAuthority *authority = (PolkitAuthority *)object;
  g_autoptr(PolkitAuthorizationResult) res = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (POLKIT_IS_AUTHORITY (authority));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(res = polkit_authority_check_authorization_finish (authority, result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else if (!polkit_authorization_result_get_is_authorized (res))
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_PROXY_AUTH_FAILED,
                             "Failed to authorize user credentials");
  else
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_helpers_get_authority_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  g_autoptr(PolkitAuthority) authority = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  PolkitSubject *subject;
  GCancellable *cancellable;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  cancellable = g_task_get_cancellable (task);
  subject = g_task_get_task_data (task);

  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (POLKIT_IS_SUBJECT (subject));

  if (!(authority = polkit_authority_get_finish (result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    polkit_authority_check_authorization (authority,
                                          subject,
                                          "org.gnome.sysprof3.profile",
                                          NULL,
                                          POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                          cancellable,
                                          sysprof_helpers_check_authorization_cb,
                                          g_steal_pointer (&task));
}

void
sysprof_helpers_authorize_async (SysprofHelpers      *self,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(PolkitSubject) subject = NULL;
  GDBusConnection *bus;
  const gchar *unique_name;

  g_return_if_fail (SYSPROF_IS_HELPERS (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_helpers_authorize_async);

  if (fail_if_no_proxy (self, task))
    return;

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (self->proxy));
  unique_name = g_dbus_connection_get_unique_name (bus);
  subject = polkit_system_bus_name_new (unique_name);
  g_task_set_task_data (task, g_steal_pointer (&subject), g_object_unref);

  polkit_authority_get_async (cancellable,
                              sysprof_helpers_get_authority_cb,
                              g_steal_pointer (&task));
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
