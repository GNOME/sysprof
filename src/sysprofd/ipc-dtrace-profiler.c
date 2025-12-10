/* ipc-dtrace-profiler.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "ipc-dtrace-profiler"

#include "config.h"

#include <dtrace.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#include <polkit/polkit.h>

#include <sysprof-capture.h>

#include "ipc-dtrace-profiler.h"

struct _IpcDtraceProfiler
{
  IpcProfilerSkeleton   parent_instance;

  GMutex                mutex;
  dtrace_hdl_t         *dtrace_hdl;
  SysprofCaptureWriter *writer;
  GThread              *consumer_thread;
  gboolean              running;
  gint                  fd;
};

static void profiler_iface_init (IpcProfilerIface *iface);

G_DEFINE_TYPE_WITH_CODE (IpcDtraceProfiler, ipc_dtrace_profiler, IPC_TYPE_PROFILER_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_TYPE_PROFILER, profiler_iface_init))

enum {
  ACTIVITY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
ipc_dtrace_profiler_activity (IpcDtraceProfiler *self)
{
  g_assert (IPC_IS_DTRACE_PROFILER (self));

  g_signal_emit (self, signals [ACTIVITY], 0);
}

static int
dtrace_chew (const dtrace_probedata_t *data,
             void                     *arg)
{
  SysprofCaptureAddress addresses[256];
  IpcDtraceProfiler *self = arg;
  guint n_addresses = 0;
  const dtrace_recdesc_t *rec;
  const char *buf;
  gint64 time;
  gint32 pid, tid;
  int cpu;

  g_assert (IPC_IS_DTRACE_PROFILER (self));
  g_assert (data != NULL);

  if (self->writer == NULL)
    return DTRACE_CONSUME_NEXT;

  time = SYSPROF_CAPTURE_CURRENT_TIME;
  cpu = data->dtpda_cpu;
  pid = data->dtpda_pid;
  tid = data->dtpda_pid;

  buf = data->dtpda_data;
  for (rec = data->dtpda_recdesc; rec != NULL; rec = rec->dtrd_next)
    {
      if (rec->dtrd_action == DTRACEACT_USTACK ||
          (rec->dtrd_action == DTRACEACT_DIFEXPR && rec->dtrd_size > 0))
        {
          const uint64_t *ustack_data = (const uint64_t *)(buf + rec->dtrd_offset);
          guint ustack_size = rec->dtrd_size / sizeof(uint64_t);

          if (ustack_size > 0)
            {
              /* ustack() typically returns frame count as first element */
              guint frame_count = ustack_data[0];

              if (frame_count > 0 && frame_count < ustack_size && frame_count < G_N_ELEMENTS (addresses))
                {
                  n_addresses = frame_count;
                  for (guint i = 0; i < n_addresses; i++)
                    addresses[i] = ustack_data[i + 1];
                }
              else if (ustack_size > 1)
                {
                  /* Try without frame count header */
                  n_addresses = MIN (ustack_size - 1, G_N_ELEMENTS (addresses));
                  for (guint i = 0; i < n_addresses; i++)
                    addresses[i] = ustack_data[i + 1];
                }
              else
                {
                  /* Direct interpretation */
                  n_addresses = MIN (ustack_size, G_N_ELEMENTS (addresses));
                  for (guint i = 0; i < n_addresses; i++)
                    addresses[i] = ustack_data[i];
                }

              break;
            }
        }
    }

  /* Fallback: try direct data extraction if records didn't work */
  if (n_addresses == 0 && data->dtpda_data != NULL && data->dtpda_size > 0)
    {
      const uint64_t *bt = (const uint64_t *)data->dtpda_data;
      guint data_size = data->dtpda_size / sizeof (uint64_t);

      n_addresses = MIN (data_size, G_N_ELEMENTS (addresses));
      for (guint i = 0; i < n_addresses; i++)
        addresses[i] = bt[i];
    }

  if (n_addresses > 0)
    {
      g_mutex_lock (&self->mutex);
      if (self->writer != NULL)
        sysprof_capture_writer_add_sample (self->writer,
                                           time,
                                           cpu,
                                           pid,
                                           tid,
                                           addresses,
                                           n_addresses);
      g_mutex_unlock (&self->mutex);
    }

  return DTRACE_CONSUME_NEXT;
}

static int
dtrace_chewrec (const dtrace_probedata_t *data,
                const dtrace_recdesc_t   *rec,
                void                     *arg)
{
  return DTRACE_CONSUME_NEXT;
}

static gpointer
dtrace_consumer_thread (gpointer data)
{
  IpcDtraceProfiler *self = data;
  int err;

  g_assert (IPC_IS_DTRACE_PROFILER (self));
  g_assert (self->dtrace_hdl != NULL);

  while (g_atomic_int_get (&self->running))
    {
      err = dtrace_work (self->dtrace_hdl, NULL, dtrace_chew, dtrace_chewrec, self);

      if (err == -1)
        {
          int dtrace_err = dtrace_errno (self->dtrace_hdl);

          if (dtrace_err != EDT_NORECORD)
            {
              g_warning ("dtrace_work failed: %s", dtrace_errmsg (self->dtrace_hdl, dtrace_err));
              break;
            }
        }

      /* XXX: Can we poll on something? Though given 997hz we can probably
       *      be sure we have data to poll soon.
       */
      g_usleep (10000); /* 10ms */
    }

  return NULL;
}

static void
ipc_dtrace_profiler_stop_locked (IpcDtraceProfiler *self)
{
  g_assert (IPC_IS_DTRACE_PROFILER (self));

  g_message ("Stopping DTrace profiler");

  g_atomic_int_set (&self->running, TRUE);

  if (self->consumer_thread != NULL)
    {
      g_thread_join (self->consumer_thread);
      self->consumer_thread = NULL;
    }

  if (self->dtrace_hdl != NULL)
    {
      dtrace_stop (self->dtrace_hdl);
      dtrace_close (self->dtrace_hdl);
      self->dtrace_hdl = NULL;
    }

  if (self->writer != NULL)
    {
      sysprof_capture_writer_flush (self->writer);
      sysprof_capture_writer_unref (self->writer);
      self->writer = NULL;
    }

  g_clear_fd (&self->fd, NULL);
}

static gboolean
ipc_dtrace_profiler_g_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                        GDBusMethodInvocation  *invocation)
{
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_DTRACE_PROFILER (skeleton));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  ipc_dtrace_profiler_activity (IPC_DTRACE_PROFILER (skeleton));

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
ipc_dtrace_profiler_finalize (GObject *object)
{
  IpcDtraceProfiler *self = (IpcDtraceProfiler *)object;

  g_mutex_lock (&self->mutex);
  ipc_dtrace_profiler_stop_locked (self);
  g_mutex_unlock (&self->mutex);
  g_mutex_clear (&self->mutex);

  G_OBJECT_CLASS (ipc_dtrace_profiler_parent_class)->finalize (object);
}

static void
ipc_dtrace_profiler_class_init (IpcDtraceProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  object_class->finalize = ipc_dtrace_profiler_finalize;

  skeleton_class->g_authorize_method = ipc_dtrace_profiler_g_authorize_method;

  /**
   * IpcDtraceProfiler::activity:
   *
   * The "activity" signal is used to denote that some amount of activity
   * has occurred and therefore the process should be kept alive longer.
   */
  signals[ACTIVITY] =
    g_signal_new ("activity",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
ipc_dtrace_profiler_init (IpcDtraceProfiler *self)
{
  g_mutex_init (&self->mutex);

  self->dtrace_hdl = NULL;
  self->writer = NULL;
  self->consumer_thread = NULL;
  self->running = FALSE;
  self->fd = -1;

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

IpcProfiler *
ipc_dtrace_profiler_new (void)
{
  return g_object_new (IPC_TYPE_DTRACE_PROFILER, NULL);
}

static gboolean
ipc_dtrace_profiler_handle_start (IpcProfiler           *profiler,
                                  GDBusMethodInvocation *invocation,
                                  GUnixFDList           *in_fd_list,
                                  GVariant              *arg_options,
                                  GVariant              *arg_fd)
{
  IpcDtraceProfiler *self = (IpcDtraceProfiler *)profiler;
  g_autoptr(GMutexLocker) locker = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *script = NULL;
  gint fd = -1;
  gint handle;
  dtrace_hdl_t *dtrace_hdl = NULL;
  gint32 pid = -1;

  g_assert (IPC_IS_DTRACE_PROFILER (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (arg_options != NULL);
  g_assert (g_variant_is_of_type (arg_options, G_VARIANT_TYPE_VARDICT));
  g_assert (arg_fd != NULL);
  g_assert (g_variant_is_of_type (arg_fd, G_VARIANT_TYPE_HANDLE));

  /* Get the FD for capture writing */
  if (in_fd_list != NULL &&
      (handle = g_variant_get_handle (arg_fd)) > -1)
    fd = g_unix_fd_list_get (in_fd_list, handle, NULL);

  /* Make sure the FD is reasonable */
  if (fd < 0)
    {
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  "Invalid param 'fd'");
      return TRUE;
    }

  /* Extract PID from options if provided */
  {
    GVariantIter iter;
    GVariant *value;
    gchar *key;

    g_variant_iter_init (&iter, arg_options);
    while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
      {
        if (g_strcmp0 (key, "pid") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
          pid = g_variant_get_int32 (value);
      }
  }

  locker = g_mutex_locker_new (&self->mutex);

  if (self->dtrace_hdl != NULL)
    {
      g_clear_fd (&fd, NULL);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  "DTrace profiler already running");
      return TRUE;
    }

  /* Initialize DTrace */
  if ((dtrace_hdl = dtrace_open (DTRACE_VERSION, 0, &errno)) == NULL)
    {
      g_clear_fd (&fd, NULL);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  g_strerror (errno));
      return TRUE;
    }

  /* Set options */
  dtrace_setopt (dtrace_hdl, "bufsize", "4m");
  dtrace_setopt (dtrace_hdl, "aggsize", "4m");

  /* Create DTrace script to sample backtraces using profile provider.
   * profile-997 means sample at 997Hz (approximately 1ms intervals)
   * We use trace() to output the ustack data which we'll extract from records.
   */
  if (pid > 0)
    script = g_strdup_printf ("profile-997 /pid == %d/ { trace(ustack()); }", pid);
  else
    script = g_strdup ("profile-997 { trace(ustack()); }");

  if (dtrace_compile (dtrace_hdl, script, NULL, DTRACE_C_ZDEFS, NULL, NULL) != 0)
    {
      g_autofree gchar *errmsg = g_strdup (dtrace_errmsg (dtrace_hdl, dtrace_errno (dtrace_hdl)));

      dtrace_close (dtrace_hdl);
      g_clear_fd (&fd, NULL);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  errmsg);
      return TRUE;
    }

  /* Create capture writer */
  self->writer = sysprof_capture_writer_new_from_fd (fd, 0);
  self->fd = fd;
  self->dtrace_hdl = dtrace_hdl;
  self->running = TRUE;

  /* Start consumer thread */
  self->consumer_thread = g_thread_new ("dtrace-consumer", dtrace_consumer_thread, self);

  /* Start DTrace */
  if (dtrace_go (dtrace_hdl) != 0)
    {
      g_autofree gchar *errmsg = g_strdup (dtrace_errmsg (dtrace_hdl, dtrace_errno (dtrace_hdl)));

      ipc_dtrace_profiler_stop_locked (self);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
						  errmsg);
      return TRUE;
    }

  g_message ("Starting DTrace profiler");

  ipc_profiler_complete_start (profiler, g_steal_pointer (&invocation), NULL);

  return TRUE;
}

static gboolean
ipc_dtrace_profiler_handle_stop (IpcProfiler           *profiler,
                                  GDBusMethodInvocation *invocation)
{
  IpcDtraceProfiler *self = (IpcDtraceProfiler *)profiler;
  g_autoptr(GMutexLocker) locker = NULL;

  g_assert (IPC_IS_DTRACE_PROFILER (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  locker = g_mutex_locker_new (&self->mutex);

  ipc_dtrace_profiler_stop_locked (self);

  ipc_profiler_complete_stop (profiler, g_steal_pointer (&invocation));

  return TRUE;
}

static void
profiler_iface_init (IpcProfilerIface *iface)
{
  iface->handle_start = ipc_dtrace_profiler_handle_start;
  iface->handle_stop = ipc_dtrace_profiler_handle_stop;
}
