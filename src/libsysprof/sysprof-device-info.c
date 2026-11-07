/* sysprof-device-info.c
 *
 * Copyright 2026 Christian Hergert
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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>

#if HAVE_LIBSYSTEMD
# include <systemd/sd-hwdb.h>
#else
typedef void sd_hwdb;
#endif

#include "sysprof-device-info-private.h"
#include "sysprof-fd-private.h"
#include "sysprof-recording-private.h"

static const char * const pci_attributes[] = {
  "vendor",
  "device",
  "subsystem_vendor",
  "subsystem_device",
  "class",
  "revision",
  "modalias",
  "power/runtime_status",
  NULL
};

static const char * const usb_attributes[] = {
  "idVendor",
  "idProduct",
  "bcdDevice",
  "bDeviceClass",
  "bDeviceSubClass",
  "bDeviceProtocol",
  "manufacturer",
  "product",
  "busnum",
  "devnum",
  "speed",
  "version",
  "power/runtime_status",
  NULL
};

static char *
read_attribute (const char *device,
                const char *attribute)
{
  g_autofree char *path = NULL;
  g_autofd int fd = -1;
  char buf[4096];
  ssize_t len;

  g_assert (device != NULL);
  g_assert (g_strv_contains (pci_attributes, attribute) ||
            g_strv_contains (usb_attributes, attribute));

  path = g_build_filename (device, attribute, NULL);

  if ((fd = open (path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)) == -1)
    return NULL;

  do
    len = read (fd, buf, sizeof buf - 1);
  while (len < 0 && errno == EINTR);

  if (len <= 0)
    return NULL;

  buf[len] = 0;
  g_strstrip (buf);

  return g_utf8_make_valid (buf, -1);
}

static GPtrArray *
list_devices (const char *path)
{
  GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GDir) dir = NULL;
  const char *name;

  g_assert (path != NULL);

  if ((dir = g_dir_open (path, 0, NULL)))
    {
      while ((name = g_dir_read_name (dir)))
        g_ptr_array_add (names, g_strdup (name));
    }

  g_ptr_array_sort_values (names, (GCompareFunc)g_strcmp0);

  return names;
}

static void
add_names (GKeyFile   *report,
           const char *group,
           const char *alias,
           sd_hwdb    *hwdb)
{
#if HAVE_LIBSYSTEMD
  static const char * const keys[] = {
    "ID_VENDOR_FROM_DATABASE",
    "ID_MODEL_FROM_DATABASE",
    "ID_PCI_CLASS_FROM_DATABASE",
    "ID_PCI_SUBCLASS_FROM_DATABASE",
    "ID_PCI_INTERFACE_FROM_DATABASE",
  };

  if (hwdb == NULL || alias == NULL)
    return;

  for (guint i = 0; i < G_N_ELEMENTS (keys); i++)
    {
      const char *value = NULL;

      if (sd_hwdb_get (hwdb, alias, keys[i], &value) >= 0)
        g_key_file_set_string (report, group, keys[i], value);
    }
#endif
}

static void
add_device (GKeyFile           *report,
            const char         *group,
            const char         *path,
            const char * const *attributes,
            sd_hwdb            *hwdb)
{
  g_autofree char *driver_path = NULL;
  g_autofree char *driver = NULL;
  g_autofree char *alias = NULL;

  g_assert (report != NULL);
  g_assert (group != NULL);
  g_assert (path != NULL);
  g_assert (attributes != NULL);

  g_key_file_set_string (report, group, "sysfs-path", path);

  for (guint i = 0; attributes[i]; i++)
    {
      g_autofree char *value = read_attribute (path, attributes[i]);

      if (value != NULL)
        g_key_file_set_string (report, group, attributes[i], value);
    }

  driver_path = g_build_filename (path, "driver", NULL);

  if ((driver = g_file_read_link (driver_path, NULL)))
    {
      g_autofree char *name = g_path_get_basename (driver);

      g_key_file_set_string (report, group, "driver", name);
    }

  if (attributes == usb_attributes)
    {
      g_autofree char *vendor = g_key_file_get_string (report, group, "idVendor", NULL);
      g_autofree char *product = g_key_file_get_string (report, group, "idProduct", NULL);

      if (vendor != NULL && product != NULL)
        alias = g_strdup_printf ("usb:v%04Xp%04X",
                                 (guint)g_ascii_strtoull (vendor, NULL, 16),
                                 (guint)g_ascii_strtoull (product, NULL, 16));
    }
  else
    {
      alias = g_key_file_get_string (report, group, "modalias", NULL);
    }

  add_names (report, group, alias, hwdb);
}

static void
collect_pci (GKeyFile   *report,
             GHashTable *devices,
             const char *sysfs,
             gboolean    graphics,
             sd_hwdb    *hwdb)
{
  g_autofree char *directory = g_build_filename (sysfs, "bus/pci/devices", NULL);
  g_autoptr(GPtrArray) names = list_devices (directory);

  for (guint i = 0; i < names->len; i++)
    {
      const char *name = g_ptr_array_index (names, i);
      g_autofree char *path = g_build_filename (directory, name, NULL);
      g_autofree char *canonical = realpath (path, NULL);
      g_autofree char *group = g_strdup_printf ("PCI %s", name);

      if (graphics)
        {
          g_autofree char *class = read_attribute (path, "class");

          if (class == NULL || (g_ascii_strtoull (class, NULL, 16) >> 16) != 3)
            continue;
        }

      add_device (report, group, path, pci_attributes, hwdb);

      if (canonical != NULL)
        g_hash_table_insert (devices, g_steal_pointer (&canonical), g_strdup (group));
    }
}

static gboolean
is_drm_node (const char *name)
{
  const char *suffix;

  if (g_str_has_prefix (name, "card"))
    suffix = name + strlen ("card");
  else if (g_str_has_prefix (name, "renderD"))
    suffix = name + strlen ("renderD");
  else
    return FALSE;

  if (*suffix == 0)
    return FALSE;

  for (; *suffix; suffix++)
    {
      if (!g_ascii_isdigit (*suffix))
        return FALSE;
    }

  return TRUE;
}

static void
collect_drm (GKeyFile   *report,
             GHashTable *devices,
             const char *sysfs,
             sd_hwdb    *hwdb)
{
  g_autofree char *directory = g_build_filename (sysfs, "class/drm", NULL);
  g_autoptr(GPtrArray) names = list_devices (directory);

  for (guint i = 0; i < names->len; i++)
    {
      const char *name = g_ptr_array_index (names, i);
      const char *group;
      g_autofree char *path = NULL;
      g_autofree char *canonical = NULL;
      g_autofree char *node = NULL;
      g_autofree char *previous = NULL;
      g_autofree char *nodes = NULL;

      if (!is_drm_node (name))
        continue;

      path = g_build_filename (directory, name, "device", NULL);
      canonical = realpath (path, NULL);

      if (canonical == NULL)
        canonical = g_build_filename (directory, name, NULL);

      if (!(group = g_hash_table_lookup (devices, canonical)))
        {
          g_autofree char *new_group = g_strdup_printf ("DRM %s", name);

          add_device (report, new_group, path, pci_attributes, hwdb);
          g_hash_table_insert (devices, g_strdup (canonical), g_steal_pointer (&new_group));
          group = g_hash_table_lookup (devices, canonical);
        }

      node = g_build_filename ("/dev/dri", name, NULL);
      previous = g_key_file_get_string (report, group, "nodes", NULL);
      nodes = previous ? g_strconcat (previous, ";", node, NULL) : g_strdup (node);
      g_key_file_set_string (report, group, "nodes", nodes);
    }
}

static void
collect_usb (GKeyFile   *report,
             const char *sysfs,
             sd_hwdb    *hwdb)
{
  g_autofree char *directory = g_build_filename (sysfs, "bus/usb/devices", NULL);
  g_autoptr(GPtrArray) names = list_devices (directory);

  for (guint i = 0; i < names->len; i++)
    {
      const char *name = g_ptr_array_index (names, i);
      g_autofree char *path = NULL;
      g_autofree char *group = NULL;

      if (strchr (name, ':') != NULL)
        continue;

      path = g_build_filename (directory, name, NULL);
      group = g_strdup_printf ("USB %s", name);
      add_device (report, group, path, usb_attributes, hwdb);
    }
}

char *
_sysprof_device_info_collect (const char *sysfs,
                              gboolean    graphics,
                              gboolean    use_hwdb)
{
  g_autoptr(GKeyFile) report = NULL;
  g_autoptr(GHashTable) devices = NULL;
  sd_hwdb *hwdb = NULL;

  g_return_val_if_fail (sysfs != NULL, NULL);

  report = g_key_file_new ();
  devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  g_key_file_set_integer (report, "Report", "version", 1);
  g_key_file_set_string (report, "Report", "collection", "cached sysfs attributes");

  if (graphics)
    g_key_file_set_string (report,
                           "Report",
                           "scope",
                           "Device identity only; graphics API capabilities are not probed");

#if HAVE_LIBSYSTEMD
  if (use_hwdb)
    sd_hwdb_new (&hwdb);
#endif

  collect_pci (report, devices, sysfs, graphics, hwdb);

  if (graphics)
    collect_drm (report, devices, sysfs, hwdb);
  else
    collect_usb (report, sysfs, hwdb);

#if HAVE_LIBSYSTEMD
  sd_hwdb_unref (hwdb);
#endif

  return g_key_file_to_data (report, NULL, NULL);
}

static DexFuture *
info_command_finish_fiber (SysprofRecording *recording,
                           GSubprocess      *subprocess,
                           const char       *name,
                           SysprofFD        *output_fd)
{
  g_autoptr(GMappedFile) output = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = sysprof_fd_steal (output_fd);

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (name != NULL);
  g_assert (fd >= 0);

  if (!dex_await (dex_subprocess_wait (subprocess), &error))
    {
      _sysprof_recording_diagnostic (recording,
                                     name,
                                     "Failed to wait for command: %s",
                                     error->message);
      g_clear_error (&error);
    }

  if ((output = g_mapped_file_new_from_fd (fd, FALSE, &error)))
    {
      if (g_mapped_file_get_length (output) > 0)
        _sysprof_recording_add_file_data (recording,
                                          name,
                                          g_mapped_file_get_contents (output),
                                          g_mapped_file_get_length (output),
                                          TRUE);
      else
        sysprof_capture_writer_add_file (recording->writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         -1,
                                         -1,
                                         name,
                                         TRUE,
                                         (const guint8 *)"",
                                         0);
    }
  else
    {
      _sysprof_recording_diagnostic (recording,
                                     name,
                                     "Failed to read command output: %s",
                                     error->message);
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
info_command_fiber (SysprofRecording   *recording,
                    const char * const *argv,
                    guint               timeout_msec,
                    GCancellable       *cancellable)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(DexFuture) finish = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *name = NULL;
  g_autofd int output_fd = -1;
  g_autofd int child_fd = -1;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (argv != NULL && argv[0] != NULL);
  g_assert (G_IS_CANCELLABLE (cancellable));
  g_assert (timeout_msec > 0);

  if (g_cancellable_is_cancelled (cancellable))
    return dex_future_new_for_boolean (TRUE);

  if ((output_fd = g_file_open_tmp ("sysprof-info-XXXXXX", &filename, &error)) == -1)
    goto failure;

  g_unlink (filename);

  if ((child_fd = dup (output_fd)) == -1)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Failed to duplicate output descriptor: %s",
                   g_strerror (errno));
      goto failure;
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDERR_MERGE);
  g_subprocess_launcher_take_stdout_fd (launcher, g_steal_fd (&child_fd));
  g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);

  if (!(subprocess = g_subprocess_launcher_spawnv (launcher, argv, &error)))
    goto failure;

  name = g_path_get_basename (argv[0]);

  if (!dex_await (dex_future_first (dex_subprocess_wait (subprocess),
                                    dex_timeout_new_usec ((gint64)timeout_msec * 1000),
                                    dex_cancellable_new_from_cancellable (cancellable),
                                    NULL),
                  &error))
    {
      g_subprocess_force_exit (subprocess);

      if (g_error_matches (error, DEX_ERROR, DEX_ERROR_TIMED_OUT))
        _sysprof_recording_diagnostic (recording, name,
                                       "Command timed out; partial output retained");
      else if (g_error_matches (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED) ||
               g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        _sysprof_recording_diagnostic (recording, name,
                                       "Command cancelled; partial output retained");
      else
        _sysprof_recording_diagnostic (recording, name,
                                       "Failed to wait for command: %s", error->message);
    }
  else if (!g_subprocess_get_successful (subprocess))
    {
      _sysprof_recording_diagnostic (recording, name,
                                     "Command failed (wait status %d); output retained",
                                     g_subprocess_get_status (subprocess));
    }

  finish = dex_scheduler_spawnv (NULL, 0,
                                 G_CALLBACK (info_command_finish_fiber),
                                 4,
                                 SYSPROF_TYPE_RECORDING, recording,
                                 G_TYPE_SUBPROCESS, subprocess,
                                 G_TYPE_STRING, name,
                                 SYSPROF_TYPE_FD, &output_fd);
  dex_future_disown (dex_ref (finish));
  dex_await (g_steal_pointer (&finish), NULL);

  return dex_future_new_for_boolean (TRUE);

failure:
  _sysprof_recording_diagnostic (recording, argv[0],
                                 "Failed to run optional command: %s", error->message);

  return dex_future_new_for_boolean (TRUE);
}

DexFuture *
_sysprof_device_info_run_command (SysprofRecording   *recording,
                                  const char * const *argv,
                                  guint               timeout_msec,
                                  GCancellable       *cancellable)
{
  dex_return_error_if_fail (SYSPROF_IS_RECORDING (recording));
  dex_return_error_if_fail (argv != NULL && argv[0] != NULL);
  dex_return_error_if_fail (G_IS_CANCELLABLE (cancellable));
  dex_return_error_if_fail (timeout_msec > 0);

  return dex_scheduler_spawnv (NULL, 0,
                               G_CALLBACK (info_command_fiber),
                               4,
                               SYSPROF_TYPE_RECORDING, recording,
                               G_TYPE_STRV, argv,
                               G_TYPE_UINT, timeout_msec,
                               G_TYPE_CANCELLABLE, cancellable);
}

typedef struct
{
  SysprofRecording *recording;
  GCancellable     *cancellable;
  gboolean          graphics;
  gboolean          detailed;
} InfoPrepare;

static void
info_prepare_free (gpointer data)
{
  InfoPrepare *prepare = data;

  g_cancellable_cancel (prepare->cancellable);
  g_clear_object (&prepare->cancellable);
  g_clear_object (&prepare->recording);
  g_free (prepare);
}

static DexFuture *
info_collect_thread (gpointer user_data)
{
  return dex_future_new_take_string (_sysprof_device_info_collect ("/sys",
                                                                   GPOINTER_TO_INT (user_data),
                                                                   TRUE));
}

static DexFuture *
info_prepare_fiber (gpointer user_data)
{
  static const char * const graphics_commands[][2] = {
    { "eglinfo", NULL },
    { "vulkaninfo", NULL },
  };
  static const char * const hardware_commands[][3] = {
    { "lspci", "-v", NULL },
    { "lsusb", "-v", NULL },
  };
  InfoPrepare *prepare = user_data;
  g_autofree char *report = NULL;

  if (g_cancellable_is_cancelled (prepare->cancellable))
    return dex_future_new_for_boolean (TRUE);

  if (!(report = dex_await_string (dex_future_first (dex_thread_spawn ("sysprof-device-info",
                                                                       info_collect_thread,
                                                                       GINT_TO_POINTER (prepare->graphics),
                                                                       NULL),
                                                     dex_cancellable_new_from_cancellable (prepare->cancellable),
                                                     NULL),
                                   NULL)))
    return dex_future_new_for_boolean (TRUE);

  _sysprof_recording_add_file_data (prepare->recording,
                                    prepare->graphics ? "graphics-info" : "hardware-info",
                                    report,
                                    -1,
                                    TRUE);

  if (prepare->detailed)
    {
      for (guint i = 0; i < 2; i++)
        {
          const char * const *argv = prepare->graphics ? graphics_commands[i] : hardware_commands[i];

          if (g_cancellable_is_cancelled (prepare->cancellable))
            break;

          if (!dex_await (_sysprof_device_info_run_command (prepare->recording,
                                                            argv,
                                                            15000,
                                                            prepare->cancellable),
                          NULL))
            break;
        }
    }

  return dex_future_new_for_boolean (TRUE);
}

DexFuture *
_sysprof_device_info_prepare (SysprofRecording *recording,
                              gboolean          graphics,
                              gboolean          detailed,
                              GCancellable     *cancellable)
{
  InfoPrepare *prepare;

  dex_return_error_if_fail (SYSPROF_IS_RECORDING (recording));
  dex_return_error_if_fail (G_IS_CANCELLABLE (cancellable));

  prepare = g_new0 (InfoPrepare, 1);
  prepare->recording = g_object_ref (recording);
  prepare->cancellable = g_object_ref (cancellable);
  prepare->graphics = graphics;
  prepare->detailed = detailed;

  return dex_scheduler_spawn (NULL,
                              0,
                              info_prepare_fiber,
                              prepare,
                              info_prepare_free);
}
