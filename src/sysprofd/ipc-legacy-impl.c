/* ipc-legacy-impl.c
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

#define G_LOG_DOMAIN "ipc-legacy-impl"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <polkit/polkit.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "../libsysprof/sysprof-kallsyms.h"
#include "helpers.h"
#include "ipc-legacy-impl.h"

struct _IpcLegacyImpl
{
  IpcLegacySysprof2Skeleton parent;
};

enum {
  ACTIVITY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static gboolean
ipc_legacy_impl_handle_perf_event_open (IpcLegacySysprof2     *service,
                                        GDBusMethodInvocation *invocation,
                                        GUnixFDList           *fd_list,
                                        GVariant              *options,
                                        gint32                 pid,
                                        gint32                 cpu,
                                        guint64                flags)
{
  gint out_fd = -1;
  gint handle;

  g_assert (IPC_IS_LEGACY_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_message ("LEGACY: PerfEventOpen(pid=%d, cpu=%d)", pid, cpu);

  errno = 0;
  if (!helpers_perf_event_open (options, pid, cpu, -1, flags, &out_fd))
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
          ipc_legacy_sysprof2_complete_perf_event_open (service,
                                                        g_steal_pointer (&invocation),
                                                        out_fd_list,
                                                        g_variant_new ("h", handle));
        }
    }

  if (out_fd != -1)
    close (out_fd);

  return TRUE;
}

static gboolean
ipc_legacy_impl_handle_get_kernal_symbols (IpcLegacySysprof2     *service,
                                           GDBusMethodInvocation *invocation)
{
  g_autoptr(SysprofKallsyms) kallsyms = NULL;
  GVariantBuilder builder;
  const gchar *name;
  guint64 addr;
  guint8 type;

  g_assert (IPC_IS_LEGACY_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_message ("LEGACY: GetKernelSymbols()");

  if (!(kallsyms = sysprof_kallsyms_new ("/proc/kallsyms")))
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to create parse kallsyms");
      return TRUE;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tys)"));
  while (sysprof_kallsyms_next (kallsyms, &name, &addr, &type))
    g_variant_builder_add (&builder, "(tys)", addr, type, name);

  ipc_legacy_sysprof2_complete_get_kernel_symbols (service,
                                                   g_steal_pointer (&invocation),
                                                   g_variant_builder_end (&builder));

  return TRUE;
}

static gboolean
ipc_legacy_impl_g_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                     GDBusMethodInvocation  *invocation)
{
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_LEGACY_IMPL (skeleton));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_signal_emit (skeleton, signals [ACTIVITY], 0);

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
sysprof2_iface_init (IpcLegacySysprof2Iface *iface)
{
  iface->handle_perf_event_open = ipc_legacy_impl_handle_perf_event_open;
  iface->handle_get_kernel_symbols = ipc_legacy_impl_handle_get_kernal_symbols;
}

G_DEFINE_TYPE_WITH_CODE (IpcLegacyImpl, ipc_legacy_impl, IPC_LEGACY_TYPE_SYSPROF2_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_LEGACY_TYPE_SYSPROF2, sysprof2_iface_init))

static void
ipc_legacy_impl_class_init (IpcLegacyImplClass *klass)
{
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  skeleton_class->g_authorize_method = ipc_legacy_impl_g_authorize_method;

  signals [ACTIVITY] =
    g_signal_new ("activity",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
ipc_legacy_impl_init (IpcLegacyImpl *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

IpcLegacySysprof2 *
ipc_legacy_impl_new (void)
{
  return g_object_new (IPC_TYPE_LEGACY_IMPL, NULL);
}
