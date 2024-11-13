/*
 * ipc-unwinder-impl.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "ipc-unwinder-impl"

#include "config.h"

#include <errno.h>
#include <fcntl.h>

#include <signal.h>
#include <sys/socket.h>

#include <glib/gstdio.h>

#include <polkit/polkit.h>

#include "ipc-unwinder-impl.h"

struct _IpcUnwinderImpl
{
  IpcUnwinderSkeleton parent_instance;
};

static void
ipc_unwinder_impl_wait_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  g_autoptr(IpcUnwinderImpl) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (IPC_IS_UNWINDER_IMPL (self));

  if (!g_subprocess_wait_check_finish (subprocess, result, &error))
    g_warning ("wait_check failure: %s", error->message);
  else
    g_info ("sysprof-live-unwinder exited");
}

static gboolean
ipc_unwinder_impl_handle_unwind (IpcUnwinder           *unwinder,
                                 GDBusMethodInvocation *invocation,
                                 GUnixFDList           *fd_list,
                                 guint                  stack_size,
                                 GVariant              *arg_perf_fds,
                                 GVariant              *arg_event_fd,
                                 GVariant              *arg_capture_fd)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int capture_fd = -1;
  g_autofd int event_fd = -1;
  GVariantIter iter;
  int next_target_fd = 3;
  int perf_fd_handle;
  int cpu;

  g_assert (IPC_IS_UNWINDER_IMPL (unwinder));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (!fd_list || G_IS_UNIX_FD_LIST (fd_list));

  if (stack_size == 0 || stack_size % sysconf (_SC_PAGESIZE) != 0)
    {
      g_dbus_method_invocation_return_error_literal (g_steal_pointer (&invocation),
                                                     G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Stack size must be a multiple of the page size");
      return TRUE;
    }

  if (fd_list == NULL)
    {
      g_dbus_method_invocation_return_error_literal (g_steal_pointer (&invocation),
                                                     G_DBUS_ERROR,
                                                     G_DBUS_ERROR_FILE_NOT_FOUND,
                                                     "Missing perf FDs");
      return TRUE;
    }

  launcher = g_subprocess_launcher_new (0);
  argv = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (argv, g_strdup (PACKAGE_LIBEXECDIR "/sysprof-live-unwinder"));
  g_ptr_array_add (argv, g_strdup_printf ("--stack-size=%u", stack_size));

  if (-1 == (event_fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (arg_event_fd), &error)) ||
      -1 == (capture_fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (arg_capture_fd), &error)))
    {
      g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
      return TRUE;
    }

  g_ptr_array_add (argv, g_strdup_printf ("--event-fd=%d", next_target_fd));
  g_subprocess_launcher_take_fd (launcher, g_steal_fd (&event_fd), next_target_fd++);

  g_variant_iter_init (&iter, arg_perf_fds);

  while (g_variant_iter_loop (&iter, "(hi)", &perf_fd_handle, &cpu))
    {
      g_autofd int perf_fd = g_unix_fd_list_get (fd_list, perf_fd_handle, &error);

      if (perf_fd < 0)
        {
          g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
          return TRUE;
        }

      g_ptr_array_add (argv, g_strdup_printf ("--perf-fd=%d:%d", next_target_fd, cpu));
      g_subprocess_launcher_take_fd (launcher,
                                     g_steal_fd (&perf_fd),
                                     next_target_fd++);
    }

  g_ptr_array_add (argv, g_strdup_printf ("--capture-fd=%d", next_target_fd));
  g_subprocess_launcher_take_fd (launcher, g_steal_fd (&capture_fd), next_target_fd++);

  g_ptr_array_add (argv, NULL);

  if (!(subprocess = g_subprocess_launcher_spawnv (launcher, (const char * const *)argv->pdata, &error)))
    {
      g_critical ("Failed to start sysprof-live-unwinder: %s", error->message);
      g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
      return TRUE;
    }

  g_message ("sysprof-live-unwinder started as process %s",
             g_subprocess_get_identifier (subprocess));

  ipc_unwinder_complete_unwind (unwinder, g_steal_pointer (&invocation), NULL);

  g_subprocess_wait_check_async (subprocess,
                                 NULL,
                                 ipc_unwinder_impl_wait_cb,
                                 g_object_ref (unwinder));

  return TRUE;
}

static void
unwinder_iface_init (IpcUnwinderIface *iface)
{
  iface->handle_unwind = ipc_unwinder_impl_handle_unwind;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (IpcUnwinderImpl, ipc_unwinder_impl, IPC_TYPE_UNWINDER_SKELETON,
                               G_IMPLEMENT_INTERFACE (IPC_TYPE_UNWINDER, unwinder_iface_init))

static gboolean
ipc_unwinder_impl_g_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                      GDBusMethodInvocation  *invocation)
{
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_UNWINDER_IMPL (skeleton));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  peer_name = g_dbus_method_invocation_get_sender (invocation);

  if (!(authority = polkit_authority_get_sync (NULL, NULL)) ||
      !(subject = polkit_system_bus_name_new (peer_name)) ||
      !(res = polkit_authority_check_authorization_sync (authority,
                                                         POLKIT_SUBJECT (subject),
                                                         "org.gnome.sysprof3.profile",
                                                         NULL,
                                                         POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                                         NULL,
                                                         NULL)) ||
      !polkit_authorization_result_get_is_authorized (res))
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Not authorized to make request");
      ret = FALSE;
    }

  g_clear_object (&authority);
  g_clear_object (&subject);
  g_clear_object (&res);

  return ret;
}

static void
ipc_unwinder_impl_finalize (GObject *object)
{
  G_OBJECT_CLASS (ipc_unwinder_impl_parent_class)->finalize (object);
}

static void
ipc_unwinder_impl_class_init (IpcUnwinderImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  object_class->finalize = ipc_unwinder_impl_finalize;

  skeleton_class->g_authorize_method = ipc_unwinder_impl_g_authorize_method;
}

static void
ipc_unwinder_impl_init (IpcUnwinderImpl *self)
{
}

IpcUnwinder *
ipc_unwinder_impl_new (void)
{
  return g_object_new (IPC_TYPE_UNWINDER_IMPL, NULL);
}
