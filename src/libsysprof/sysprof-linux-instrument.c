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

#include "../sysprofd/helpers.h"

#define PROCESS_INFO_KEYS "pid,maps,mountinfo,cmdline,comm,cgroup"

struct _SysprofLinuxInstrument
{
  SysprofInstrument  parent_instance;
  GDBusConnection   *connection;
  SysprofRecording  *recording;
  GHashTable        *seen;
};

G_DEFINE_FINAL_TYPE (SysprofLinuxInstrument, sysprof_linux_instrument, SYSPROF_TYPE_INSTRUMENT)

static char **
sysprof_linux_instrument_list_required_policy (SysprofInstrument *instrument)
{
  static const char *policy[] = {"org.gnome.sysprof3.profile", NULL};

  return g_strdupv ((char **)policy);
}

static void
sysprof_linux_instrument_set_connection (SysprofInstrument *instrument,
                                         GDBusConnection   *connection)
{
  SysprofLinuxInstrument *self = SYSPROF_LINUX_INSTRUMENT (instrument);

  g_set_object (&self->connection, connection);
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
add_process_info (SysprofLinuxInstrument *self,
                  SysprofRecording       *recording,
                  GVariant               *process_info,
                  gint64                  at_time)
{
  g_autoptr(SysprofPodman) podman = NULL;
  g_autoptr(GPtrArray) futures = NULL;
  SysprofCaptureWriter *writer;
  GVariantIter iter;
  GVariant *pidinfo;

  g_assert (SYSPROF_IS_LINUX_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
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

      g_hash_table_add (self->seen, GINT_TO_POINTER (pid));

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

static void
get_process_info_task (gpointer data)
{
  g_autoptr(DexPromise) promise = data;
  g_autoptr(GVariant) info = NULL;

  if (!(info = helpers_get_process_info (PROCESS_INFO_KEYS)))
    dex_promise_reject (promise,
                        g_error_new (G_IO_ERROR,
                                     G_IO_ERROR_PERMISSION_DENIED,
                                     "Failed to load process info"));
  else
    dex_promise_resolve_variant (promise, g_steal_pointer (&info));
}

static DexFuture *
get_process_info (void)
{
  DexPromise *promise = dex_promise_new ();
  dex_scheduler_push (dex_thread_pool_scheduler_get_default (),
                      get_process_info_task,
                      dex_ref (promise));
  return DEX_FUTURE (g_steal_pointer (&promise));
}

static DexFuture *
sysprof_linux_instrument_prepare_fiber (gpointer user_data)
{
  SysprofLinuxInstrument *self = user_data;
  g_autoptr(GVariant) process_info_reply = NULL;
  g_autoptr(GVariant) process_info = NULL;
  g_autoptr(GError) error = NULL;
  gint64 at_time;

  g_assert (SYSPROF_IS_LINUX_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));

  /* First get some basic information about the system into the capture. We can
   * get the contents for all of these concurrently.
   */
  if (!dex_await (dex_future_all (_sysprof_recording_add_file (self->recording, "/proc/cpuinfo", TRUE),
                                  _sysprof_recording_add_file (self->recording, "/proc/mounts", TRUE),
                                  NULL),
                  &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  at_time = SYSPROF_CAPTURE_CURRENT_TIME;

  if (self->connection != NULL)
    {
      /* We also want to get a bunch of info on user processes so that we can add
       * records about them to the recording.
       */
      if (!(process_info_reply = dex_await_variant (dex_dbus_connection_call (self->connection,
                                                                              "org.gnome.Sysprof3",
                                                                              "/org/gnome/Sysprof3",
                                                                              "org.gnome.Sysprof3.Service",
                                                                              "GetProcessInfo",
                                                                              g_variant_new ("(s)", PROCESS_INFO_KEYS),
                                                                              G_VARIANT_TYPE ("(aa{sv})"),
                                                                              G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                                              G_MAXINT),
                                                    &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));

      /* Add process records for each of the processes discovered */
      process_info = g_variant_get_child_value (process_info_reply, 0);
    }
  else
    {
      /* Load process info using same mechanism as sysprofd */
      if (!(process_info = dex_await_variant (get_process_info (), &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

  g_assert (process_info != NULL);

  dex_await (add_process_info (self, self->recording, process_info, at_time), NULL);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_prepare (SysprofInstrument *instrument,
                                  SysprofRecording  *recording)
{
  SysprofLinuxInstrument *self = (SysprofLinuxInstrument *)instrument;

  g_assert (SYSPROF_IS_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_linux_instrument_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

typedef struct
{
  SysprofRecording *recording;
  GPtrArray *paths;
  int pid;
} ProcessStarted;

static void
process_started_free (gpointer data)
{
  ProcessStarted *state = data;

  g_clear_object (&state->recording);
  g_clear_pointer (&state->paths, g_ptr_array_unref);
  g_free (state);
}

static DexFuture *
process_started_cb (DexFuture *completed,
                    gpointer   user_data)
{
  ProcessStarted *state = user_data;
  guint size;

  g_assert (DEX_IS_FUTURE_SET (completed));
  g_assert (state != NULL);
  g_assert (SYSPROF_IS_RECORDING (state->recording));
  g_assert (state->pid > 0);
  g_assert (state->paths != NULL);

  size = dex_future_set_get_size (DEX_FUTURE_SET (completed));

  g_assert (state->paths->len == size);

  for (guint i = 0; i < size; i++)
    {
      g_autoptr(GError) error = NULL;
      const GValue *value = NULL;
      GVariant *variant = NULL;
      const char *path = g_ptr_array_index (state->paths, i);
      const char *bytestring = NULL;

      if (!(value = dex_future_set_get_value_at (DEX_FUTURE_SET (completed), i, &error)))
        continue;

      if (!(variant = g_value_get_variant (value)) ||
          !g_variant_is_of_type (variant, G_VARIANT_TYPE ("(ay)")))
        g_return_val_if_reached (NULL);

      g_variant_get (variant, "(^&ay)", &bytestring);

      if (bytestring == NULL)
        g_return_val_if_reached (NULL);

      _sysprof_recording_add_file_data (state->recording, path, bytestring, -1, TRUE);
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_process_started (SysprofInstrument *instrument,
                                          SysprofRecording  *recording,
                                          int                pid,
                                          const char        *comm)
{
  SysprofLinuxInstrument *self = (SysprofLinuxInstrument *)instrument;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *mountinfo_path = NULL;
  g_autofree char *flatpak_info_path = NULL;
  DexFuture *mountinfo;
  DexFuture *flatpak_info;
  ProcessStarted *state;

  g_assert (SYSPROF_IS_LINUX_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  /* Most information for new processes we'll get inline, like the address
   * layout for memory mappings. But we still want to get information about
   * flatpak and mountinfo.
   */

  /* Avoid spawn amplification by monitoring processes which are spawned in
   * reaction to the requests we make of sysprofd. This in particular sucks
   * because it means we are not very good at profiling these processes (at
   * least the ones spawned while recording). However, if we did try to get
   * information on them, we'd end up just chaising our tail.
   */
  if (comm != NULL)
    {
      /* Only check up to 15 chars because that is what we can get from Perf
       * via PERF_RECORD_COMM messages.
       */
      if (g_str_has_prefix (comm, "systemd-userwor") ||
          g_str_has_prefix (comm, "pkla-check-auth"))
        return dex_future_new_for_boolean (TRUE);
    }

  /* Shortcut if we've already seen this */
  if (g_hash_table_contains (self->seen, GINT_TO_POINTER (pid)))
    return dex_future_new_for_boolean (TRUE);
  g_hash_table_add (self->seen, GINT_TO_POINTER (pid));

  /* Get the bus synchronously so we don't have to suspend the fiber. This
   * will always return immediately anyway.
   */
  if (!(bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  mountinfo_path = g_strdup_printf ("/proc/%d/mountinfo", pid);
  mountinfo = dex_dbus_connection_call (bus,
                                        "org.gnome.Sysprof3",
                                        "/org/gnome/Sysprof3",
                                        "org.gnome.Sysprof3.Service",
                                        "GetProcFile",
                                        g_variant_new ("(^ay)", mountinfo_path),
                                        G_VARIANT_TYPE ("(ay)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1);

  flatpak_info_path = g_strdup_printf ("/proc/%d/root/.flatpak-info", pid);
  flatpak_info = dex_dbus_connection_call (bus,
                                           "org.gnome.Sysprof3",
                                           "/org/gnome/Sysprof3",
                                           "org.gnome.Sysprof3.Service",
                                           "GetProcFile",
                                           g_variant_new ("(^ay)", flatpak_info_path),
                                           G_VARIANT_TYPE ("(ay)"),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1);

  state = g_new0 (ProcessStarted, 1);
  state->pid = pid;
  state->recording = g_object_ref (recording);
  state->paths = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (state->paths, g_steal_pointer (&mountinfo_path));
  g_ptr_array_add (state->paths, g_steal_pointer (&flatpak_info_path));

  return dex_future_finally (dex_future_all (mountinfo, flatpak_info, NULL),
                             process_started_cb,
                             state,
                             process_started_free);
}

static void
sysprof_linux_instrument_dispose (GObject *object)
{
  SysprofLinuxInstrument *self = (SysprofLinuxInstrument *)object;

  g_clear_object (&self->connection);
  g_clear_object (&self->recording);
  g_hash_table_remove_all (self->seen);

  G_OBJECT_CLASS (sysprof_linux_instrument_parent_class)->dispose (object);
}

static void
sysprof_linux_instrument_finalize (GObject *object)
{
  SysprofLinuxInstrument *self = (SysprofLinuxInstrument *)object;

  g_clear_pointer (&self->seen, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_linux_instrument_parent_class)->finalize (object);
}

static void
sysprof_linux_instrument_class_init (SysprofLinuxInstrumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_linux_instrument_dispose;
  object_class->finalize = sysprof_linux_instrument_finalize;

  instrument_class->list_required_policy = sysprof_linux_instrument_list_required_policy;
  instrument_class->prepare = sysprof_linux_instrument_prepare;
  instrument_class->process_started = sysprof_linux_instrument_process_started;
  instrument_class->set_connection = sysprof_linux_instrument_set_connection;
}

static void
sysprof_linux_instrument_init (SysprofLinuxInstrument *self)
{
  self->seen = g_hash_table_new (NULL, NULL);
}

SysprofInstrument *
_sysprof_linux_instrument_new (void)
{
  return g_object_new (SYSPROF_TYPE_LINUX_INSTRUMENT, NULL);
}
