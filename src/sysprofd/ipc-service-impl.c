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

#include "ipc-service-impl.h"

struct _IpcServiceImpl
{
  IpcServiceSkeleton parent_instance;
};

static gboolean
ipc_service_impl_handle_list_processes (IpcService            *service,
                                        GDBusMethodInvocation *invocation)
{
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GArray) pids = NULL;
  const gchar *name;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (!(dir = g_dir_open ("/proc/", 0, NULL)))
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FILE_NOT_FOUND,
                                             "Failed to access /proc");
      return TRUE;
    }

  pids = g_array_new (FALSE, FALSE, sizeof (gint32));

  while ((name = g_dir_read_name (dir)))
    {
      if (g_ascii_isalnum (*name))
        {
          gchar *endptr = NULL;
          gint64 val = g_ascii_strtoll (name, &endptr, 10);

          if (endptr != NULL && *endptr == 0 && val < G_MAXINT && val >= 0)
            {
              gint32 v32 = val;
              g_array_append_val (pids, v32);
            }
        }
    }

  ipc_service_complete_list_processes (service,
                                       g_steal_pointer (&invocation),
                                       g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                                                  pids->data,
                                                                  pids->len,
                                                                  sizeof (gint32)));

  return TRUE;
}

static gboolean
ipc_service_impl_handle_get_proc_file (IpcService            *service,
                                       GDBusMethodInvocation *invocation,
                                       const gchar           *path)
{
  g_autofree gchar *canon = NULL;
  g_autofree gchar *contents = NULL;
  gsize len;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  canon = g_canonicalize_filename (path, "/proc/");

  if (!g_str_has_prefix (canon, "/proc/"))
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_ACCESS_DENIED,
                                           "File is not within /proc/");
  else if (!g_file_get_contents (canon, &contents, &len, NULL))
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_FILE_NOT_FOUND,
                                           "Failed to locate the file");
  else
    ipc_service_complete_get_proc_file (service,
                                        g_steal_pointer (&invocation),
                                        contents);

  return TRUE;
}

static void
init_service_iface (IpcServiceIface *iface)
{
  iface->handle_list_processes = ipc_service_impl_handle_list_processes;
  iface->handle_get_proc_file = ipc_service_impl_handle_get_proc_file;
}

G_DEFINE_TYPE_WITH_CODE (IpcServiceImpl, ipc_service_impl, IPC_TYPE_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_TYPE_SERVICE, init_service_iface))

static void
ipc_service_impl_class_init (IpcServiceImplClass *klass)
{
}

static void
ipc_service_impl_init (IpcServiceImpl *self)
{
}

IpcService *
ipc_service_impl_new (void)
{
  return g_object_new (IPC_TYPE_SERVICE_IMPL, NULL);
}
