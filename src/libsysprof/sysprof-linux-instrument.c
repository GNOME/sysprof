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

#include <stdio.h>

#include "sysprof-linux-instrument-private.h"
#include "sysprof-maps-parser-private.h"
#include "sysprof-podman-private.h"
#include "sysprof-recording-private.h"

#include "line-reader-private.h"

struct _SysprofLinuxInstrument
{
  SysprofInstrument parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofLinuxInstrument, sysprof_linux_instrument, SYSPROF_TYPE_INSTRUMENT)

static char **
sysprof_linux_instrument_list_required_policy (SysprofInstrument *instrument)
{
  static const char *policy[] = {"org.gnome.sysprof3.profile", NULL};

  return g_strdupv ((char **)policy);
}

static void
add_mmaps (SysprofRecording *recording,
           GPid              pid,
           const char       *mapsstr,
           gboolean          ignore_inode,
           gint64            at_time)
{
  SysprofCaptureWriter *writer;
  SysprofMapsParser parser;
  guint64 begin, end, offset, inode;
  char *file;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (mapsstr != NULL);
  g_assert (pid > 0);

  writer = _sysprof_recording_writer (recording);

  sysprof_maps_parser_init (&parser, mapsstr, -1);
  while (sysprof_maps_parser_next (&parser, &begin, &end, &offset, &inode, &file))
    {
      if (ignore_inode)
        inode = 0;

      sysprof_capture_writer_add_map (writer, at_time, -1, pid,
                                      begin, end, offset, inode,
                                      file);
      g_free (file);
    }
}

static DexFuture *
populate_overlays (SysprofRecording *recording,
                   SysprofPodman    *podman,
                   int               pid,
                   const char       *cgroup,
                   gint64            at_time)
{
  static GRegex *flatpak_regex;
  static GRegex *podman_regex;

  g_autoptr(GMatchInfo) flatpak_match = NULL;
  g_autoptr(GMatchInfo) podman_match = NULL;
  SysprofCaptureWriter *writer;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (cgroup != NULL);

  if (strcmp (cgroup, "") == 0)
    return dex_future_new_for_boolean (TRUE);

  writer = _sysprof_recording_writer (recording);

  /* This function tries to discover the podman container that contains the
   * process identified on the host as @pid. We can only do anything with this
   * if the pids are in containers that the running user controls (so that we
   * can actually access the overlays).
   *
   * This stuff, and I want to emphasize, is a giant hack. Just like containers
   * are on Linux. But if we are really careful, we can make this work for the
   * one particular use case I care about, which is podman/toolbox on Fedora
   * Workstation/Silverblue.
   *
   * -- Christian
   */
  if G_UNLIKELY (podman_regex == NULL)
    {
      podman_regex = g_regex_new ("libpod-([a-z0-9]{64})\\.scope", G_REGEX_OPTIMIZE, 0, NULL);
      g_assert (podman_regex != NULL);
    }

  if (flatpak_regex == NULL)
    {
      flatpak_regex = g_regex_new ("app-flatpak-([a-zA-Z_\\-\\.]+)-[0-9]+\\.scope", G_REGEX_OPTIMIZE, 0, NULL);
      g_assert (flatpak_regex != NULL);
    }

  if (g_regex_match (podman_regex, cgroup, 0, &podman_match))
    {
      g_autofree char *word = g_match_info_fetch (podman_match, 1);
      g_autofree char *path = g_strdup_printf ("/proc/%d/root/run/.containerenv", pid);
      g_auto(GStrv) layers = NULL;

      if ((layers = sysprof_podman_get_layers (podman, word)))
        {
          for (guint i = 0; layers[i]; i++)
            sysprof_capture_writer_add_overlay (writer, at_time, -1, pid,
                                                i, layers[i], "/");
        }

      return _sysprof_recording_add_file (recording, path, FALSE);
    }
  else if (g_regex_match (flatpak_regex, cgroup, 0, &flatpak_match))
    {
      g_autofree char *path = g_strdup_printf ("/proc/%d/root/.flatpak-info", pid);

      return _sysprof_recording_add_file (recording, path, FALSE);
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
add_process_info (SysprofRecording *recording,
                  GVariant         *process_info,
                  gint64            at_time)
{
  g_autoptr(SysprofPodman) podman = NULL;
  g_autoptr(GPtrArray) futures = NULL;
  SysprofCaptureWriter *writer;
  GVariantIter iter;
  GVariant *pidinfo;

  g_assert (process_info != NULL);
  g_assert (g_variant_is_of_type (process_info, G_VARIANT_TYPE ("aa{sv}")));

  writer = _sysprof_recording_writer (recording);
  podman = sysprof_podman_snapshot_current_user ();
  futures = g_ptr_array_new_with_free_func (dex_unref);

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
      gboolean ignore_inode;
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
      sysprof_capture_writer_add_process (writer, at_time, -1, pid,
                                          *cmdline ? cmdline : comm);

      /* Give the capture access to the mountinfo of that process to aid
       * in resolving symbols later on.
       */
      mount_path = g_strdup_printf ("/proc/%u/mountinfo", pid);
      _sysprof_recording_add_file_data (recording, mount_path, mountinfo, -1, TRUE);

      /* Ignore inodes from podman/toolbox because they appear to always be
       * wrong. We'll have to rely on CRC/build-id instead.
       */
      ignore_inode = strstr (cgroup, "/libpod-") != NULL;
      add_mmaps (recording, pid, maps, ignore_inode, at_time);

      /* We might have overlays that need to be applied to the process
       * which can be rather combursome for old-style Podman using
       * FUSE overlayfs.
       */
      g_ptr_array_add (futures, populate_overlays (recording, podman, pid, cgroup, at_time));

      skip:
        g_variant_dict_clear (&dict);
    }

  if (futures->len > 0)
    return dex_future_allv ((DexFuture **)futures->pdata, futures->len);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_prepare_fiber (gpointer user_data)
{
  SysprofRecording *recording = user_data;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GVariant) process_info_reply = NULL;
  g_autoptr(GVariant) process_info = NULL;
  g_autoptr(GError) error = NULL;
  gint64 at_time;

  g_assert (SYSPROF_IS_RECORDING (recording));

  /* First get some basic information about the system into the capture. We can
   * get the contents for all of these concurrently.
   */
  if (!dex_await (dex_future_all (_sysprof_recording_add_file (recording, "/proc/cpuinfo", TRUE),
                                  _sysprof_recording_add_file (recording, "/proc/mounts", TRUE),
                                  NULL),
                  &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* We need access to the bus to call various sysprofd API directly */
  if (!(bus = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* We also want to get a bunch of info on user processes so that we can add
   * records about them to the recording.
   */
  at_time = SYSPROF_CAPTURE_CURRENT_TIME;
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
  dex_await (add_process_info (recording, process_info, at_time), NULL);

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
sysprof_linux_instrument_process_started (SysprofInstrument *instrument,
                                          SysprofRecording  *recording,
                                          int                pid,
                                          const char        *comm)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  /* We can't actually make a request of sysprofd to get
   * information on the process right now because that would
   * create a new process to verify our authentication.
   *
   * And that would just amplify this request further.
   */

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_linux_instrument_class_init (SysprofLinuxInstrumentClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->list_required_policy = sysprof_linux_instrument_list_required_policy;
  instrument_class->prepare = sysprof_linux_instrument_prepare;
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
