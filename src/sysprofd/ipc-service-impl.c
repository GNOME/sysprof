/* ipc-service-impl.c
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

#define G_LOG_DOMAIN "ipc-service-impl"

#include "config.h"

#include <errno.h>
#include <gio/gunixfdlist.h>
#include <polkit/polkit.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "helpers.h"
#include "ipc-service-impl.h"

struct _IpcServiceImpl
{
  IpcServiceSkeleton parent_instance;
};

static gboolean
ipc_service_impl_handle_list_processes (IpcService            *service,
                                        GDBusMethodInvocation *invocation)
{
  g_autofree gint32 *processes = NULL;
  gsize n_processes = 0;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (!helpers_list_processes (&processes, &n_processes))
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_NOT_SUPPORTED,
                                           "Failed to access processes");
  else
    ipc_service_complete_list_processes (service,
                                         g_steal_pointer (&invocation),
                                         g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                                                    processes,
                                                                    n_processes,
                                                                    sizeof (gint32)));

  return TRUE;
}

static gboolean
ipc_service_impl_handle_get_proc_file (IpcService            *service,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *path)
{
  g_autofree gchar *contents = NULL;
  gsize len;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (!helpers_get_proc_file (path, &contents, &len))
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "Failed to load proc file");
  else
    ipc_service_complete_get_proc_file (service,
                                        g_steal_pointer (&invocation),
                                        contents);

  return TRUE;
}

static gboolean
ipc_service_impl_handle_perf_event_open (IpcService            *service,
                                         GDBusMethodInvocation *invocation,
                                         GVariant              *options,
                                         gint32                 pid,
                                         gint32                 cpu,
                                         GVariant              *group_fdv,
                                         guint64                flags)
{
  GUnixFDList *in_fd_list = NULL;
  GDBusMessage *message;
  gint group_fd = -1;
  gint out_fd = -1;
  gint handle;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_message ("PerfEventOpen(pid=%d, cpu=%d)", pid, cpu);

  /* Get the group_fd if provided */
  message = g_dbus_method_invocation_get_message (invocation);
  if ((in_fd_list = g_dbus_message_get_unix_fd_list (message)) &&
      (handle = g_variant_get_handle (group_fdv)) > -1)
    group_fd = g_unix_fd_list_get (in_fd_list, handle, NULL);

  errno = 0;
  if (!helpers_perf_event_open (options, pid, cpu, group_fd, flags, &out_fd))
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to create perf counter: %s",
                                             g_strerror (errno));
    }
  else
    {
      g_autoptr(GUnixFDList) out_fd_list = g_unix_fd_list_new ();
      g_autoptr(GError) error = NULL;

      if (-1 == (handle = g_unix_fd_list_append (out_fd_list, out_fd, &error)))
        {
          g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_LIMITS_EXCEEDED,
                                                 "Failed to create file-descriptor for reply");
        }
      else
        {
          g_dbus_method_invocation_return_value_with_unix_fd_list (g_steal_pointer (&invocation),
                                                                   g_variant_new ("(h)", handle),
                                                                   out_fd_list);
        }
    }

  if (out_fd != -1)
    close (out_fd);

  if (group_fd != -1)
    close (group_fd);

  return TRUE;
}

static gboolean
ipc_service_impl_g_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                     GDBusMethodInvocation  *invocation)
{
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_SERVICE_IMPL (skeleton));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  peer_name = g_dbus_method_invocation_get_sender (invocation);

  if (!(authority = polkit_authority_get_sync (NULL, NULL)) ||
      !(subject = polkit_system_bus_name_new (peer_name)) ||
      !polkit_authority_check_authorization_sync (authority,
                                                  POLKIT_SUBJECT (subject),
                                                  "org.gnome.sysprof3.profile",
                                                  NULL,
                                                  POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                                  NULL,
                                                  NULL))
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Not authorized to make request");
      ret = FALSE;
    }

  g_clear_object (&authority);
  g_clear_object (&subject);

  return ret;
}

static void
init_service_iface (IpcServiceIface *iface)
{
  iface->handle_list_processes = ipc_service_impl_handle_list_processes;
  iface->handle_get_proc_file = ipc_service_impl_handle_get_proc_file;
  iface->handle_perf_event_open = ipc_service_impl_handle_perf_event_open;
}

G_DEFINE_TYPE_WITH_CODE (IpcServiceImpl, ipc_service_impl, IPC_TYPE_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_TYPE_SERVICE, init_service_iface))

static void
ipc_service_impl_class_init (IpcServiceImplClass *klass)
{
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  skeleton_class->g_authorize_method = ipc_service_impl_g_authorize_method;
}

static void
ipc_service_impl_init (IpcServiceImpl *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

IpcService *
ipc_service_impl_new (void)
{
  return g_object_new (IPC_TYPE_SERVICE_IMPL, NULL);
}
