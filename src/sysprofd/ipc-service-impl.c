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
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <polkit/polkit.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "helpers.h"
#include "ipc-service-impl.h"

struct _IpcServiceImpl
{
  IpcServiceSkeleton parent_instance;
};

enum {
  ACTIVITY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static gboolean
file_set_contents_no_backup (const gchar  *path,
                             const gchar  *contents,
                             gssize        len,
                             GError      **error)
{
  int fd;

  g_return_val_if_fail (path != NULL, FALSE);

  if (contents == NULL)
    contents = "";

  if (len < 0)
    len = strlen (contents);

  /* This is only for setting files in /sys, which need to be a single
   * write anyway, so don't try to incrementally write at all.
   */

  fd = open (path, O_WRONLY, 0644);

  if (fd == -1)
    {
      int errsv = errno;
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errsv),
                   "%s", g_strerror (errsv));
      return FALSE;
    }

  ftruncate (fd, 0);

  if (write (fd, contents, len) != len)
    {
      int errsv = errno;
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errsv),
                   "%s", g_strerror (errsv));
      close (fd);
      return FALSE;
    }

  close (fd);

  return TRUE;
}

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
ipc_service_impl_handle_get_proc_fd (IpcService            *service,
                                     GDBusMethodInvocation *invocation,
                                     GUnixFDList           *in_fd_list,
                                     const gchar           *path)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *canon = NULL;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  file = g_file_new_for_path (path);
  canon = g_file_get_path (file);

  if (g_str_has_prefix (canon, "/proc/") || g_str_has_prefix (canon, "/sys/"))
    {
      gint fd = open (canon, O_RDONLY | O_CLOEXEC);

      if (fd != -1)
        {
          g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new ();
          gint handle = g_unix_fd_list_append (fd_list, fd, &error);

          close (fd);

          if (handle != -1)
            {
              ipc_service_complete_get_proc_fd (service,
                                                g_steal_pointer (&invocation),
                                                fd_list,
                                                g_variant_new ("h", handle));
              return TRUE;
            }
        }
    }

  g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_ACCESS_DENIED,
                                         "Failed to load proc fd");

  return TRUE;
}

static gboolean
ipc_service_impl_handle_perf_event_open (IpcService            *service,
                                         GDBusMethodInvocation *invocation,
                                         GUnixFDList           *in_fd_list,
                                         GVariant              *options,
                                         gint32                 pid,
                                         gint32                 cpu,
                                         GVariant              *group_fdv,
                                         guint64                flags)
{
  gint group_fd = -1;
  gint out_fd = -1;
  gint handle;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_message ("PerfEventOpen(pid=%d, cpu=%d)", pid, cpu);

  /* Get the group_fd if provided */
  if (in_fd_list != NULL &&
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
          ipc_service_complete_perf_event_open (service,
                                                g_steal_pointer (&invocation),
                                                out_fd_list,
                                                g_variant_new ("h", handle));
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
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_SERVICE_IMPL (skeleton));
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

static gboolean
ipc_service_impl_handle_get_process_info (IpcService            *service,
                                          GDBusMethodInvocation *invocation,
                                          const gchar           *attributes)
{
  g_autoptr(GVariant) res = NULL;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (attributes != NULL);

  res = helpers_get_process_info (attributes);
  ipc_service_complete_get_process_info (service, invocation, res);

  return TRUE;
}

static gboolean
ipc_service_impl_handle_set_governor (IpcService            *service,
                                      GDBusMethodInvocation *invocation,
                                      const gchar           *governor)
{
  g_autofree gchar *available = NULL;
  g_autofree gchar *previous = NULL;
  gboolean had_error = FALSE;
  guint n_procs;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (governor != NULL);

  if (!g_file_get_contents ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors", &available, NULL, NULL))
    {
      g_warning ("Failed to discover available governors");
      had_error = TRUE;
      goto finish;
    }

  if (strstr (available, governor) == NULL)
    {
      /* No such governor */
      g_warning ("No such governor \"%s\"", governor);
      had_error = TRUE;
      goto finish;
    }

  if (g_file_get_contents ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", &previous, NULL, NULL))
    g_strstrip (previous);
  else
    previous = g_strdup ("unknown");

  n_procs = g_get_num_processors ();

  for (guint i = 0; i < n_procs; i++)
    {
      g_autofree gchar *path = g_strdup_printf ("/sys/devices/system/cpu/cpu%u/cpufreq/scaling_governor", i);
      g_autoptr(GError) error = NULL;

      if (!file_set_contents_no_backup (path, governor, -1, &error))
        {
          g_warning ("Failed to set governor on CPU %u: %s", i, error->message);
          had_error = TRUE;
        }
    }

finish:
  if (had_error)
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_FAILED,
                                           "Failed to set governor");
  else
    ipc_service_complete_set_governor (service,
                                       g_steal_pointer (&invocation),
                                       previous);

  return TRUE;
}

static gboolean
ipc_service_impl_handle_set_paranoid (IpcService            *service,
                                      GDBusMethodInvocation *invocation,
                                      int                    paranoid)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *str = NULL;
  int previous = G_MAXINT;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  paranoid = CLAMP (paranoid, -1, 2);

  if (!g_file_get_contents ("/proc/sys/kernel/perf_event_paranoid", &str, NULL, &error))
    {
      g_warning ("Failed to discover previous perf_event_paranoid setting: %s", error->message);
      goto finish;
    }

  previous = g_ascii_strtoll (str, NULL, 10);

  if (previous != paranoid)
    {
      char paranoid_str[8];
      gssize len = g_snprintf (paranoid_str, sizeof paranoid_str, "%d", paranoid);

      if (!file_set_contents_no_backup ("/proc/sys/kernel/perf_event_paranoid", paranoid_str, len, &error))
        g_warning ("Failed to set perf_event_paranoid: %s", error->message);
    }

finish:
  if (error != NULL)
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_FAILED,
                                           "Failed to set perf_event_paranoid: %s",
                                           error->message);
  else
    ipc_service_complete_set_paranoid (service,
                                       g_steal_pointer (&invocation),
                                       previous);

  return TRUE;
}

static void
init_service_iface (IpcServiceIface *iface)
{
  iface->handle_list_processes = ipc_service_impl_handle_list_processes;
  iface->handle_get_proc_file = ipc_service_impl_handle_get_proc_file;
  iface->handle_get_proc_fd = ipc_service_impl_handle_get_proc_fd;
  iface->handle_perf_event_open = ipc_service_impl_handle_perf_event_open;
  iface->handle_get_process_info = ipc_service_impl_handle_get_process_info;
  iface->handle_set_governor = ipc_service_impl_handle_set_governor;
  iface->handle_set_paranoid = ipc_service_impl_handle_set_paranoid;
}

G_DEFINE_TYPE_WITH_CODE (IpcServiceImpl, ipc_service_impl, IPC_TYPE_SERVICE_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_TYPE_SERVICE, init_service_iface))

static void
ipc_service_impl_class_init (IpcServiceImplClass *klass)
{
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  skeleton_class->g_authorize_method = ipc_service_impl_g_authorize_method;

  signals [ACTIVITY] =
    g_signal_new ("activity",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
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
