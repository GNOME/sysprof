/* ipc-rapl-profiler.c
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

#define G_LOG_DOMAIN "ipc-rapl-profiler"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <polkit/polkit.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "ipc-rapl-profiler.h"
#include "sysprof-turbostat.h"

#define DEFAULT_POLL_FREQ_SECONDS 1

struct _IpcRaplProfiler
{
  IpcProfilerSkeleton   parent_instance;

  GMutex                mutex;
  GArray               *counter_ids;
  SysprofTurbostat     *turbostat;
  SysprofCaptureWriter *writer;
  guint                 poll_source;
};

typedef struct
{
  gint  core;
  gint  cpu;
  guint counter_base;
} CounterId;

static void profiler_iface_init (IpcProfilerIface *iface);

G_DEFINE_TYPE_WITH_CODE (IpcRaplProfiler, ipc_rapl_profiler, IPC_TYPE_PROFILER_SKELETON,
                         G_IMPLEMENT_INTERFACE (IPC_TYPE_PROFILER, profiler_iface_init))

enum {
  ACTIVITY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
ipc_rapl_profiler_activity (IpcRaplProfiler *self)
{
  g_assert (IPC_IS_RAPL_PROFILER (self));

  g_signal_emit (self, signals [ACTIVITY], 0);
}

static void
ipc_rapl_profiler_stop_locked (IpcRaplProfiler *self)
{
  g_assert (IPC_IS_RAPL_PROFILER (self));

  g_message ("Stopping RAPL monitor");

  g_clear_handle_id (&self->poll_source, g_source_remove);

  if (self->turbostat != NULL)
    sysprof_turbostat_stop (self->turbostat);

  g_clear_pointer (&self->turbostat, sysprof_turbostat_unref);
  g_clear_pointer (&self->counter_ids, g_array_unref);

  if (self->writer != NULL)
    {
      sysprof_capture_writer_flush (self->writer);
      sysprof_capture_writer_unref (self->writer);
      self->writer = NULL;
    }
}

static guint
add_counter_base (SysprofCaptureWriter *writer,
                  GArray               *counters,
                  gint                  core,
                  gint                  cpu)
{
  static const gchar *names[] = { "Package", "Core", "GFX", "RAM" };
  g_autofree gchar *desc = NULL;
  CounterId id;

  g_assert (writer != NULL);
  g_assert (counters != NULL);

  id.core = core;
  id.cpu = cpu;
  id.counter_base = sysprof_capture_writer_request_counter (writer, G_N_ELEMENTS (names));

  g_array_append_val (counters, id);

  desc = g_strdup_printf ("Core:%u CPU:%u", core, cpu);

  {
    SysprofCaptureCounter ctrs[4] = {
      { .name = "Package Watt",
        .id = id.counter_base,
        .type = SYSPROF_CAPTURE_COUNTER_DOUBLE },

      { .name = "Core Watt",
        .id = id.counter_base + 1,
        .type = SYSPROF_CAPTURE_COUNTER_DOUBLE },

      { .name = "GFX Watt",
        .id = id.counter_base + 2,
        .type = SYSPROF_CAPTURE_COUNTER_DOUBLE },

      { .name = "RAM Watt",
        .id = id.counter_base + 3,
        .type = SYSPROF_CAPTURE_COUNTER_DOUBLE },
    };

    for (guint j = 0; j < G_N_ELEMENTS (ctrs); j++)
      {
        if (cpu == -1)
          {
            g_snprintf (ctrs[j].category, sizeof ctrs[j].category, "RAPL");
            g_snprintf (ctrs[j].name, sizeof ctrs[j].name, "%s Watt", names[j]);
          }
        else
          {
            g_snprintf (ctrs[j].category, sizeof ctrs[j].category, "RAPL %d:%d", core, cpu);
            g_snprintf (ctrs[j].name, sizeof ctrs[j].name, "%s Watt (%d:%d)", names[j], core, cpu);
          }
      }

      sysprof_capture_writer_define_counters (writer,
                                              SYSPROF_CAPTURE_CURRENT_TIME,
                                              -1,
                                              -1,
                                              ctrs,
                                              G_N_ELEMENTS (ctrs));
  }

  return id.counter_base;
}

static guint
get_counter_base (SysprofCaptureWriter *writer,
                  GArray               *counters,
                  gint                  core,
                  gint                  cpu)
{
  for (guint i = 0; i < counters->len; i++)
    {
      const CounterId *ele = &g_array_index (counters, CounterId, i);

      if (core == ele->core && cpu == ele->cpu)
        return ele->counter_base;
    }

  return add_counter_base (writer, counters, core, cpu);
}

static gboolean
ipc_rapl_profiler_g_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                      GDBusMethodInvocation  *invocation)
{
  PolkitAuthorizationResult *res = NULL;
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  const gchar *peer_name;
  gboolean ret = TRUE;

  g_assert (IPC_IS_RAPL_PROFILER (skeleton));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  ipc_rapl_profiler_activity (IPC_RAPL_PROFILER (skeleton));

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
ipc_rapl_profiler_finalize (GObject *object)
{
  IpcRaplProfiler *self = (IpcRaplProfiler *)object;

  ipc_rapl_profiler_stop_locked (self);
  g_mutex_clear (&self->mutex);

  G_OBJECT_CLASS (ipc_rapl_profiler_parent_class)->finalize (object);
}

static void
ipc_rapl_profiler_class_init (IpcRaplProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

  object_class->finalize = ipc_rapl_profiler_finalize;

  skeleton_class->g_authorize_method = ipc_rapl_profiler_g_authorize_method;

  /**
   * IpcRaplProfiler::activity:
   *
   * The "activity" signal is used to denote that some amount of activity
   * has occurred and therefore the process should be kept alive longer.
   */
  signals [ACTIVITY] =
    g_signal_new ("activity",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
ipc_rapl_profiler_init (IpcRaplProfiler *self)
{
  g_mutex_init (&self->mutex);
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

IpcProfiler *
ipc_rapl_profiler_new (void)
{
  return g_object_new (IPC_TYPE_RAPL_PROFILER, NULL);
}

static gboolean
ipc_rapl_profiler_poll_cb (gpointer data)
{
  IpcRaplProfiler *self = data;
  g_autoptr(GMutexLocker) locker = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (IPC_IS_RAPL_PROFILER (self));

  ipc_rapl_profiler_activity (self);

  locker = g_mutex_locker_new (&self->mutex);

  if (self->turbostat == NULL)
    goto failure;

  g_assert (self->counter_ids != NULL);
  g_assert (self->writer != NULL);

  if (!sysprof_turbostat_sample (self->turbostat, &error))
    {
      ipc_rapl_profiler_stop_locked (self);
      goto failure;
    }

  return G_SOURCE_CONTINUE;

failure:
  self->poll_source = 0;

  return G_SOURCE_REMOVE;
}

static void
on_sample_cb (gpointer data,
              gpointer user_data)
{
  IpcRaplProfiler *self = user_data;
  GArray *samples = data;

  g_assert (IPC_IS_RAPL_PROFILER (self));
  g_assert (samples != NULL);
  g_assert (samples->len > 0);

  for (guint i = 0; i < samples->len; i++)
    {
      const SysprofTurbostatSample *sample = &g_array_index (samples, SysprofTurbostatSample, i);
      guint base = get_counter_base (self->writer, self->counter_ids, sample->core, sample->cpu);
      const guint counter_ids[4] = { base, base + 1, base + 2, base + 3 };
      const SysprofCaptureCounterValue values[4] = {
        { .vdbl = sample->pkg_watt },
        { .vdbl = sample->core_watt },
        { .vdbl = sample->gfx_watt },
        { .vdbl = sample->ram_watt },
      };
      gboolean r;

      r = sysprof_capture_writer_set_counters (self->writer,
                                               SYSPROF_CAPTURE_CURRENT_TIME,
                                               sample->cpu,
                                               -1,
                                               counter_ids,
                                               values,
                                               G_N_ELEMENTS (values));

      if (r == FALSE)
        {
          ipc_rapl_profiler_stop_locked (self);
          return;
        }
    }

  sysprof_capture_writer_flush (self->writer);
}

static gboolean
ipc_rapl_profiler_handle_start (IpcProfiler           *profiler,
                                GDBusMethodInvocation *invocation,
                                GUnixFDList           *in_fd_list,
                                GVariant              *arg_options,
                                GVariant              *arg_fd)
{
  IpcRaplProfiler *self = (IpcRaplProfiler *)profiler;
  g_autoptr(SysprofTurbostat) turbostat = NULL;
  g_autoptr(GMutexLocker) locker = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *path = NULL;
  gint fd = -1;
  gint handle;

  g_assert (IPC_IS_RAPL_PROFILER (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (arg_options != NULL);
  g_assert (g_variant_is_of_type (arg_options, G_VARIANT_TYPE_VARDICT));
  g_assert (arg_fd != NULL);
  g_assert (g_variant_is_of_type (arg_fd, G_VARIANT_TYPE_HANDLE));

  if (!(path = g_find_program_in_path ("turbostat")))
    {
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  "Program `turbostat` is not available. Try installing kernel-tools.");
      return TRUE;
    }

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

  locker = g_mutex_locker_new (&self->mutex);

  if (self->turbostat != NULL)
    {
      close (fd);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  "RAPL collector already running");
      return TRUE;
    }

  turbostat = sysprof_turbostat_new (on_sample_cb, self);

  if (!sysprof_turbostat_start (turbostat, &error))
    {
      close (fd);
      g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&invocation),
                                                  "org.freedesktop.DBus.Error.Failed",
                                                  error->message);
      return TRUE;
    }

  self->writer = sysprof_capture_writer_new_from_fd (fd, 0);
  self->counter_ids = g_array_new (FALSE, FALSE, sizeof (CounterId));

  g_message ("Starting RAPL monitor");

  self->turbostat = g_steal_pointer (&turbostat);
  self->poll_source = g_timeout_add_seconds (DEFAULT_POLL_FREQ_SECONDS,
                                             ipc_rapl_profiler_poll_cb,
                                             self);

  ipc_profiler_complete_start (profiler, g_steal_pointer (&invocation), NULL);

  return TRUE;
}

static gboolean
ipc_rapl_profiler_handle_stop (IpcProfiler           *profiler,
                               GDBusMethodInvocation *invocation)
{
  IpcRaplProfiler *self = (IpcRaplProfiler *)profiler;
  g_autoptr(GMutexLocker) locker = NULL;

  g_assert (IPC_IS_RAPL_PROFILER (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  locker = g_mutex_locker_new (&self->mutex);

  ipc_rapl_profiler_stop_locked (self);

  ipc_profiler_complete_stop (profiler, g_steal_pointer (&invocation));

  return TRUE;
}

static void
profiler_iface_init (IpcProfilerIface *iface)
{
  iface->handle_start = ipc_rapl_profiler_handle_start;
  iface->handle_stop = ipc_rapl_profiler_handle_stop;
}
