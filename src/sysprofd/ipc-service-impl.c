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
#ifdef __linux__
# include <linux/capability.h>
# include <linux/perf_event.h>
#endif
#include <polkit/polkit.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

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

#ifdef __linux__
static int
_perf_event_open (struct perf_event_attr *attr,
                  pid_t                   pid,
                  int                     cpu,
                  int                     group_fd,
                  unsigned long           flags)
{
  g_assert (attr != NULL);

  /* Quick sanity check */
  if (attr->sample_period < 100000 && attr->type != PERF_TYPE_TRACEPOINT)
    return -EINVAL;

  return syscall (__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static gboolean
ipc_service_impl_handle_perf_event_open (IpcService            *service,
                                         GDBusMethodInvocation *invocation,
                                         GVariant              *options,
                                         gint                   pid,
                                         gint                   cpu,
                                         guint64                flags)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  struct perf_event_attr attr = {0};
  GVariantIter iter;
  GVariant *value;
  gchar *key;
  gint32 disabled = 0;
  gint32 wakeup_events = 149;
  gint32 type = 0;
  guint64 sample_period = 0;
  guint64 sample_type = 0;
  guint64 config = 0;
  gint clockid = CLOCK_MONOTONIC;
  gint comm = 0;
  gint mmap_ = 0;
  gint task = 0;
  gint exclude_idle = 0;
  gint fd = -1;
  gint handle;
  gint use_clockid = 0;
  gint sample_id_all = 0;

  g_assert (IPC_IS_SERVICE_IMPL (service));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (pid < -1 || cpu < -1)
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "pid and cpu must be >= -1");
      return TRUE;
    }

  g_variant_iter_init (&iter, options);

  while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
    {
      if (FALSE) {}
      else if (strcmp (key, "disabled") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          disabled = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "wakeup_events") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            goto bad_arg;
          wakeup_events = g_variant_get_uint32 (value);
        }
      else if (strcmp (key, "sample_id_all") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          sample_id_all = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "clockid") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
            goto bad_arg;
          clockid = g_variant_get_int32 (value);
        }
      else if (strcmp (key, "comm") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          comm = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "exclude_idle") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          exclude_idle = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "mmap") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          mmap_ = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "config") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          config = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "sample_period") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          sample_period = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "sample_type") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            goto bad_arg;
          sample_type = g_variant_get_uint64 (value);
        }
      else if (strcmp (key, "task") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          task = g_variant_get_boolean (value);
        }
      else if (strcmp (key, "type") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            goto bad_arg;
          type = g_variant_get_uint32 (value);
        }
      else if (strcmp (key, "use_clockid") == 0)
        {
          if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            goto bad_arg;
          use_clockid = g_variant_get_boolean (value);
        }

      continue;

    bad_arg:
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid type %s for option %s",
                                             g_variant_get_type_string (value),
                                             key);
      g_clear_pointer (&value, g_variant_unref);
      g_clear_pointer (&key, g_free);
      return TRUE;
    }

  attr.comm = !!comm;
  attr.config = config;
  attr.disabled = disabled;
  attr.exclude_idle = !!exclude_idle;
  attr.mmap = !!mmap_;
  attr.sample_id_all = sample_id_all;
  attr.sample_period = sample_period;
  attr.sample_type = sample_type;
  attr.task = !!task;
  attr.type = type;
  attr.wakeup_events = wakeup_events;

#ifdef HAVE_PERF_CLOCKID
  if (!use_clockid || clockid < 0)
    attr.clockid = CLOCK_MONOTONIC_RAW;
  else
    attr.clockid = clockid;
  attr.use_clockid = use_clockid;
#endif

  attr.size = sizeof attr;

  errno = 0;
  fd = _perf_event_open (&attr, pid, cpu, -1, 0);

  if (fd < 0)
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to open perf event stream: %s",
                                             g_strerror (errno));
    }

  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, fd, NULL);

  if (handle < 0)
    {
      g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to send Unix FD List");
      goto close_fd;
    }

  g_dbus_method_invocation_return_value_with_unix_fd_list (g_steal_pointer (&invocation),
                                                           g_variant_new_handle (handle),
                                                           fd_list);

close_fd:
  if (fd != -1)
    close (fd);

  return TRUE;
}
#endif

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
#ifdef __linux__
  iface->handle_perf_event_open = ipc_service_impl_handle_perf_event_open;
#endif
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
