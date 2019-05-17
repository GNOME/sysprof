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
              g_dbus_method_invocation_return_value_with_unix_fd_list (g_steal_pointer (&invocation),
                                                                       g_variant_new ("(h)", handle),
                                                                       fd_list);
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
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_SERVICE_IMPL (skeleton));
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

static gboolean
needs_escape (const gchar *str)
{
  for (; *str; str++)
    {
      if (g_ascii_isspace (*str) || *str == '\'' || *str == '"')
        return TRUE;
    }

  return FALSE;
}

static void
postprocess_cmdline (gchar **str,
                     gsize   len)
{
  g_autoptr(GPtrArray) parts = g_ptr_array_new_with_free_func (g_free);
  g_autofree gchar *instr = NULL;
  const gchar *begin = NULL;

  if (len == 0)
    return;

  instr = *str;

  for (gsize i = 0; i < len; i++)
    {
      if (!begin && instr[i])
        {
          begin = &instr[i];
        }
      else if (begin && instr[i] == '\0')
        {
          if (needs_escape (begin))
            g_ptr_array_add (parts, g_shell_quote (begin));
          else
            g_ptr_array_add (parts, g_strdup (begin));

          begin = NULL;
        }
    }

  /* If the last byte was not \0, as can happen with prctl(), then we need
   * to add it here manually.
   */
  if (begin)
    {
      if (needs_escape (begin))
        g_ptr_array_add (parts, g_shell_quote (begin));
      else
        g_ptr_array_add (parts, g_strdup (begin));
    }

  g_ptr_array_add (parts, NULL);

  *str = g_strjoinv (" ", (gchar **)parts->pdata);
}

static void
postprocess_rstrip (gchar **str,
                    gsize   len)
{
  g_strchomp (*str);
}

static void
add_pid_proc_file_to (gint          pid,
                      const gchar  *name,
                      GVariantDict *dict,
                      void (*postprocess) (gchar **, gsize))
{
  g_autofree gchar *path = NULL;
  g_autofree gchar *contents = NULL;
  gsize len;

  g_assert (pid > -1);
  g_assert (name != NULL);
  g_assert (dict != NULL);

  path = g_strdup_printf ("/proc/%d/%s", pid, name);

  if (g_file_get_contents (path, &contents, &len, NULL))
    {
      if (postprocess)
        postprocess (&contents, len);
      g_variant_dict_insert (dict, name, "s", contents);
    }
}

static gboolean
ipc_service_impl_handle_get_process_info (IpcService            *service,
                                          GDBusMethodInvocation *invocation,
                                          const gchar           *attributes)
{
  GVariantBuilder builder;
  g_autofree gint *processes = NULL;
  gsize n_processes = 0;
  gboolean want_statm;
  gboolean want_cmdline;
  gboolean want_comm;
  gboolean want_maps;
  gboolean want_mountinfo;

  g_assert (IPC_IS_SERVICE (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (attributes != NULL);

  want_statm = !!strstr (attributes, "statm");
  want_cmdline = !!strstr (attributes, "cmdline");
  want_maps = !!strstr (attributes, "maps");
  want_mountinfo = !!strstr (attributes, "mountinfo");
  want_comm = !!strstr (attributes, "comm");

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  if (helpers_list_processes (&processes, &n_processes))
    {
      for (guint i = 0; i < n_processes; i++)
        {
          gint pid = processes[i];
          GVariantDict dict;

          g_variant_dict_init (&dict, NULL);
          g_variant_dict_insert (&dict, "pid", "i", pid, NULL);

          if (want_statm)
            add_pid_proc_file_to (pid, "statm", &dict, postprocess_rstrip);

          if (want_cmdline)
            add_pid_proc_file_to (pid, "cmdline", &dict, postprocess_cmdline);

          if (want_comm)
            add_pid_proc_file_to (pid, "comm", &dict, postprocess_rstrip);

          if (want_maps)
            add_pid_proc_file_to (pid, "maps", &dict, postprocess_rstrip);

          if (want_mountinfo)
            add_pid_proc_file_to (pid, "mountinfo", &dict, postprocess_rstrip);

          g_variant_builder_add_value (&builder, g_variant_dict_end (&dict));
        }
    }

  ipc_service_complete_get_process_info (service, invocation, g_variant_builder_end (&builder));

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
