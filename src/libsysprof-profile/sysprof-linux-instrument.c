/* sysprof-linux-instrument.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include "sysprof-linux-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofLinuxInstrument
{
  SysprofInstrument parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofLinuxInstrument, sysprof_linux_instrument, SYSPROF_TYPE_INSTRUMENT)

static char **
sysprof_linux_instrument_list_required_policy (SysprofInstrument *instrument)
{
  static const char *policy[] = {"org.gnome.sysprof3.profile", NULL};

  return g_strdupv ((char **)policy);
}

static void
add_process_info (SysprofRecording *recording,
                  GVariant         *process_info)
{
  g_autoptr(GPtrArray) futures = NULL;
  SysprofCaptureWriter *writer;
  GVariantIter iter;
  GVariant *pidinfo;

  g_assert (process_info != NULL);
  g_assert (g_variant_is_of_type (process_info, G_VARIANT_TYPE ("aa{sv}")));

  writer = _sysprof_recording_writer (recording);

  /* Loop through all the PIDs the server notified us about */
  g_variant_iter_init (&iter, process_info);
  while (g_variant_iter_loop (&iter, "@a{sv}", &pidinfo))
    {
      g_autofree char *mount_path = NULL;
      GVariantDict dict;
      const char *cmdline;
      const char *comm;
      const char *mountinfo;
      const char *maps;
      const char *cgroup;
      gint32 pid;

      g_variant_dict_init (&dict, pidinfo);

      if (!g_variant_dict_lookup (&dict, "pid", "i", &pid))
        goto skip;

      if (!g_variant_dict_lookup (&dict, "cmdline", "&s", &cmdline))
        cmdline = "";

      if (!g_variant_dict_lookup (&dict, "comm", "&s", &comm))
        comm = "";

      if (!g_variant_dict_lookup (&dict, "mountinfo", "&s", &mountinfo))
        mountinfo = "";

      if (!g_variant_dict_lookup (&dict, "maps", "&s", &maps))
        maps = "";

      if (!g_variant_dict_lookup (&dict, "cgroup", "&s", &cgroup))
        cgroup = "";

      /* Notify the capture that a process was spawned */
      sysprof_capture_writer_add_process (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          pid,
                                          *cmdline ? cmdline : comm);

      /* Give the capture access to the mountinfo of that process to aid
       * in resolving symbols later on.
       */
      mount_path = g_strdup_printf ("/proc/%u/mountinfo", pid);
      _sysprof_recording_add_file_data (recording, mount_path, mountinfo, -1);

      // TODO
      //sysprof_proc_source_populate_maps (self, pid, maps, ignore_inode);
      //sysprof_proc_source_populate_overlays (self, pid, cgroup);

      skip:
        g_variant_dict_clear (&dict);
    }
}

static DexFuture *
sysprof_linux_instrument_prepare_fiber (gpointer user_data)
{
  SysprofRecording *recording = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_RECORDING (recording));

  /* First get some basic information about the system into the capture. We can
   * get the contents for all of these concurrently.
   */
  if (!dex_await (dex_future_all (_sysprof_recording_add_file (recording, "/proc/kallsyms", TRUE),
                                  _sysprof_recording_add_file (recording, "/proc/cpuinfo", TRUE),
                                  _sysprof_recording_add_file (recording, "/proc/mounts", TRUE),
                                  NULL),
                  &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_prepare (SysprofInstrument *instrument,
                                  SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_linux_instrument_prepare_fiber,
                              g_object_ref (recording),
                              g_object_unref);
}

static DexFuture *
sysprof_linux_instrument_record_fiber (gpointer user_data)
{
  SysprofRecording *recording = user_data;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GVariant) process_info_reply = NULL;
  g_autoptr(GVariant) process_info = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_RECORDING (recording));

  /* We need access to the bus to call various sysprofd API directly */
  if (!(bus = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* We also want to get a bunch of info on user processes so that we can add
   * records about them to the recording.
   */
  if (!(process_info_reply = dex_await_variant (dex_dbus_connection_call (bus,
                                                                          "org.gnome.Sysprof3",
                                                                          "/org/gnome/Sysprof3",
                                                                          "org.gnome.Sysprof3.Service",
                                                                          "GetProcessInfo",
                                                                          g_variant_new ("(s)", "pid,maps,mountinfo,cmdline,comm,cgroup"),
                                                                          G_VARIANT_TYPE ("(aa{sv})"),
                                                                          G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                                          G_MAXINT),
                                                &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Add process records for each of the processes discovered */
  process_info = g_variant_get_child_value (process_info_reply, 0);
  add_process_info (recording, process_info);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_record (SysprofInstrument *instrument,
                                 SysprofRecording  *recording,
                                 GCancellable      *cancellable)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_linux_instrument_record_fiber,
                              g_object_ref (recording),
                              g_object_unref);
}

static DexFuture *
sysprof_linux_instrument_process_started (SysprofInstrument *instrument,
                                          SysprofRecording  *recording,
                                          int                pid)
{
  g_autofree char *mountinfo_path = NULL;

  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  mountinfo_path = g_strdup_printf ("/proc/%u/mountinfo", pid);

  return _sysprof_recording_add_file (recording, mountinfo_path, TRUE);
}

static void
sysprof_linux_instrument_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_linux_instrument_parent_class)->finalize (object);
}

static void
sysprof_linux_instrument_class_init (SysprofLinuxInstrumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_linux_instrument_finalize;

  instrument_class->list_required_policy = sysprof_linux_instrument_list_required_policy;
  instrument_class->prepare = sysprof_linux_instrument_prepare;
  instrument_class->record = sysprof_linux_instrument_record;
  instrument_class->process_started = sysprof_linux_instrument_process_started;
}

static void
sysprof_linux_instrument_init (SysprofLinuxInstrument *self)
{
}

SysprofInstrument *
_sysprof_linux_instrument_new (void)
{
  return g_object_new (SYSPROF_TYPE_LINUX_INSTRUMENT, NULL);
}
