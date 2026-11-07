/* test-device-info.c
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
#include <signal.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <sysprof.h>

#include "sysprof-device-info-private.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static char *test_root;
static char *tools_path;
static char *invocations_path;

static void
write_file (const char *relative,
            const char *contents)
{
  g_autofree char *path = g_build_filename (test_root, relative, NULL);
  g_autofree char *parent = g_path_get_dirname (path);

  g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

static void
make_link (const char *relative,
           const char *target)
{
  g_autofree char *path = g_build_filename (test_root, relative, NULL);
  g_autofree char *parent = g_path_get_dirname (path);
  g_autofree char *destination = g_build_filename (test_root, target, NULL);

  g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
  g_assert_cmpint (symlink (destination, path), ==, 0);
}

static void
assert_value (GKeyFile   *report,
              const char *group,
              const char *key,
              const char *expected)
{
  g_autofree char *value = g_key_file_get_string (report, group, key, NULL);

  g_assert_cmpstr (value, ==, expected);
}

static void
test_inventory (void)
{
  static const char * const watched[] = {
    "bus/pci/devices/0000:00:02.0/config",
    "bus/pci/devices/0000:00:02.0/rom",
    "bus/pci/devices/0000:00:02.0/resource",
    "bus/pci/devices/0000:00:02.0/resource0",
    "bus/usb/devices/1-2/descriptors",
    NULL
  };
  g_autoptr(GKeyFile) graphics = g_key_file_new ();
  g_autoptr(GKeyFile) hardware = g_key_file_new ();
  g_autofree char *first = NULL;
  g_autofree char *second = NULL;
  g_autofree char *all = NULL;
  g_autofd int monitor = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
  char events[4096];

  write_file ("bus/pci/devices/0000:00:02.0/class", "0x030000\n");
  write_file ("bus/pci/devices/0000:00:02.0/vendor", "0x8086\n");
  write_file ("bus/pci/devices/0000:00:02.0/device", "0x1234\n");
  write_file ("bus/pci/devices/0000:00:02.0/power/runtime_status", "suspended\n");
  write_file ("bus/pci/devices/0000:03:00.0/class", "0x020000\n");
  write_file ("bus/pci/devices/0000:00:02.0/config", "forbidden");
  write_file ("bus/pci/devices/0000:00:02.0/rom", "forbidden");
  write_file ("bus/pci/devices/0000:00:02.0/resource", "forbidden");
  write_file ("bus/pci/devices/0000:00:02.0/resource0", "forbidden");

  make_link ("bus/pci/devices/0000:00:02.0/driver", "drivers/test-gpu");
  make_link ("class/drm/card0/device", "bus/pci/devices/0000:00:02.0");
  make_link ("class/drm/renderD128/device", "bus/pci/devices/0000:00:02.0");
  make_link ("class/drm/card0-HDMI-A-1/device", "bus/pci/devices/0000:00:02.0");

  write_file ("bus/usb/devices/1-2/idVendor", "1234\n");
  write_file ("bus/usb/devices/1-2/idProduct", "5678\n");
  write_file ("bus/usb/devices/1-2/product", "Cached device name\n");
  write_file ("bus/usb/devices/1-2/manufacturer", "Cached vendor name\n");
  write_file ("bus/usb/devices/1-2/descriptors", "forbidden");
  write_file ("bus/usb/devices/1-2:1.0/idVendor", "1234\n");

  g_assert_cmpint (monitor, >=, 0);

  for (guint i = 0; watched[i]; i++)
    {
      g_autofree char *path = g_build_filename (test_root, watched[i], NULL);

      g_assert_cmpint (inotify_add_watch (monitor, path, IN_OPEN), >=, 0);
    }

  first = _sysprof_device_info_collect (test_root, TRUE, FALSE);
  second = _sysprof_device_info_collect (test_root, TRUE, FALSE);
  all = _sysprof_device_info_collect (test_root, FALSE, FALSE);

  g_assert_cmpstr (first, ==, second);
  g_assert_true (g_key_file_load_from_data (graphics, first, -1, 0, NULL));
  g_assert_true (g_key_file_load_from_data (hardware, all, -1, 0, NULL));

  assert_value (graphics, "PCI 0000:00:02.0", "vendor", "0x8086");
  assert_value (graphics, "PCI 0000:00:02.0", "driver", "test-gpu");
  assert_value (graphics, "PCI 0000:00:02.0", "power/runtime_status", "suspended");
  assert_value (graphics, "PCI 0000:00:02.0", "nodes", "/dev/dri/card0;/dev/dri/renderD128");
  g_assert_false (g_key_file_has_group (graphics, "PCI 0000:03:00.0"));
  g_assert_false (g_key_file_has_group (graphics, "DRM card0"));
  g_assert_null (strstr (first, "HDMI"));
  g_assert_null (strstr (first, "FROM_DATABASE"));
  g_assert_null (strstr (first, "USB"));

  g_assert_true (g_key_file_has_group (hardware, "PCI 0000:03:00.0"));
  assert_value (hardware, "USB 1-2", "idVendor", "1234");
  assert_value (hardware, "USB 1-2", "product", "Cached device name");
  g_assert_false (g_key_file_has_group (hardware, "USB 1-2:1.0"));

  g_assert_cmpint (read (monitor, events, sizeof events), ==, -1);
  g_assert_cmpint (errno, ==, EAGAIN);
}

static void
wait_future (DexFuture *future)
{
  g_autoptr(GError) error = NULL;

  while (dex_future_is_pending (future))
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (dex_await_boolean (future, &error));
  g_assert_no_error (error);
}

static SysprofRecording *
new_recording (void)
{
  SysprofRecording *recording = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  g_autofree char *path = NULL;
  int fd = g_file_open_tmp ("sysprof-info-test-XXXXXX", &path, NULL);

  g_assert_cmpint (fd, >=, 0);
  g_unlink (path);

  recording->writer = sysprof_capture_writer_new_from_fd (fd, 0);
  g_assert_nonnull (recording->writer);

  return recording;
}

static char *
read_attachment (SysprofRecording *recording,
                 const char       *name)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_true (sysprof_capture_writer_flush (recording->writer));

  loader = sysprof_document_loader_new_for_fd (_sysprof_capture_writer_dup_fd (recording->writer), &error);
  g_assert_no_error (error);

  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);

  file = sysprof_document_lookup_file (document, name);

  if (file == NULL)
    return NULL;

  bytes = sysprof_document_file_dup_bytes (file);

  return g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
}

static void
write_tool (const char *name,
            const char *body)
{
  g_autofree char *path = g_build_filename (tools_path, name, NULL);
  g_autofree char *script = g_strconcat ("#!/bin/sh\n", body, NULL);

  g_assert_true (g_file_set_contents (path, script, -1, NULL));
  g_assert_cmpint (g_chmod (path, 0700), ==, 0);
}

static void
test_prepare (void)
{
  g_autoptr(SysprofInstrument) graphics = sysprof_graphics_info_new ();
  g_autoptr(SysprofInstrument) hardware = sysprof_hardware_info_new ();
  g_autoptr(SysprofRecording) basic = new_recording ();
  g_autoptr(SysprofRecording) detailed = new_recording ();
  g_autofree char *graphics_report = NULL;
  g_autofree char *hardware_report = NULL;
  g_autofree char *calls = NULL;

  g_assert_false (sysprof_graphics_info_get_detailed (SYSPROF_GRAPHICS_INFO (graphics)));
  g_assert_false (sysprof_hardware_info_get_detailed (SYSPROF_HARDWARE_INFO (hardware)));

  wait_future (SYSPROF_INSTRUMENT_GET_CLASS (graphics)->prepare (graphics, basic));
  wait_future (SYSPROF_INSTRUMENT_GET_CLASS (hardware)->prepare (hardware, basic));
  g_assert_false (g_file_test (invocations_path, G_FILE_TEST_EXISTS));

  graphics_report = read_attachment (basic, "graphics-info");
  hardware_report = read_attachment (basic, "hardware-info");

  g_assert_nonnull (graphics_report);
  g_assert_nonnull (hardware_report);
  g_assert_null (read_attachment (basic, "eglinfo"));
  g_assert_null (read_attachment (basic, "lspci"));

  g_object_set (graphics,
                "detailed", TRUE,
                NULL);
  sysprof_hardware_info_set_detailed (SYSPROF_HARDWARE_INFO (hardware), TRUE);

  wait_future (SYSPROF_INSTRUMENT_GET_CLASS (graphics)->prepare (graphics, detailed));
  wait_future (SYSPROF_INSTRUMENT_GET_CLASS (hardware)->prepare (hardware, detailed));

  g_assert_true (g_file_get_contents (invocations_path, &calls, NULL, NULL));
  g_assert_cmpstr (calls, ==, "eglinfo\nvulkaninfo\nlspci -v\nlsusb -v\n");

  for (guint i = 0; i < 4; i++)
    {
      const char *names[] = { "eglinfo", "vulkaninfo", "lspci", "lsusb" };
      g_autofree char *output = read_attachment (detailed, names[i]);

      g_assert_nonnull (output);
      g_assert_nonnull (strstr (output, names[i]));
    }
}

static gboolean
cancel_cb (gpointer data)
{
  g_cancellable_cancel (data);

  return G_SOURCE_REMOVE;
}

static void
test_command (gconstpointer user_data)
{
  const char *mode = user_data;
  g_autoptr(SysprofRecording) recording = new_recording ();
  g_autoptr(GCancellable) cancellable = g_cancellable_new ();
  g_autofree char *path = g_build_filename (tools_path, mode, NULL);
  g_autofree char *output = NULL;
  const char *argv[] = { path, NULL };
  gboolean slow = g_str_equal (mode, "timeout") || g_str_equal (mode, "cancel");
  gint64 begin = g_get_monotonic_time ();

  if (slow)
    write_tool (mode, "echo $$\necho partial\nexec /bin/sleep 30\n");
  else if (g_str_equal (mode, "failure"))
    write_tool (mode, "echo partial\necho problem >&2\nexit 7\n");

  if (g_str_equal (mode, "cancel"))
    g_timeout_add_full (G_PRIORITY_DEFAULT,
                        100,
                        cancel_cb,
                        g_object_ref (cancellable),
                        g_object_unref);

  wait_future (_sysprof_device_info_run_command (recording,
                                                 argv,
                                                 g_str_equal (mode, "timeout") ? 100 : 5000,
                                                 cancellable));
  g_assert_cmpint (g_get_monotonic_time () - begin, <, 3 * G_USEC_PER_SEC);

  output = read_attachment (recording, mode);

  if (g_str_equal (mode, "missing"))
    g_assert_null (output);
  else
    g_assert_nonnull (output);

  if (slow)
    {
      int pid = (int)g_ascii_strtoll (output, NULL, 10);

      g_assert_cmpint (pid, >, 0);
      g_assert_nonnull (strstr (output, "partial"));
      g_assert_cmpint (kill (pid, 0), ==, -1);
      g_assert_cmpint (errno, ==, ESRCH);
      g_assert_cmpint (waitpid (pid, NULL, WNOHANG), ==, -1);
      g_assert_cmpint (errno, ==, ECHILD);
    }
  else if (g_str_equal (mode, "failure"))
    g_assert_cmpstr (output, ==, "partial\nproblem\n");

  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (recording->diagnostics)),
                    ==, 1);
}

static void
test_command_discard (void)
{
  g_autoptr(SysprofRecording) recording = new_recording ();
  g_autoptr(SysprofCaptureWriter) writer = sysprof_capture_writer_ref (recording->writer);
  g_autoptr(GCancellable) cancellable = g_cancellable_new ();
  g_autoptr(DexFuture) future = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = g_build_filename (tools_path, "discard", NULL);
  g_autofree char *output = NULL;
  const char *argv[] = { path, NULL };
  SysprofRecording *weak_recording = recording;
  gint64 deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;
  int pid;

  write_tool ("discard", "echo $$\necho partial\nexec /bin/sleep 30\n");
  g_object_add_weak_pointer (G_OBJECT (recording), (gpointer *)&weak_recording);

  future = dex_future_first (_sysprof_device_info_run_command (recording, argv, 5000, cancellable),
                             dex_timeout_new_msec (100),
                             NULL);
  g_clear_object (&recording);

  while (dex_future_is_pending (future) || weak_recording != NULL)
    {
      g_assert_cmpint (g_get_monotonic_time (), <, deadline);
      g_main_context_iteration (NULL, FALSE);
      g_usleep (1000);
    }

  g_assert_false (dex_await_boolean (g_steal_pointer (&future), &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TIMED_OUT);
  g_assert_false (g_cancellable_is_cancelled (cancellable));

  recording = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  recording->writer = g_steal_pointer (&writer);
  output = read_attachment (recording, "discard");
  g_assert_nonnull (output);
  g_assert_nonnull (strstr (output, "partial"));

  pid = (int)g_ascii_strtoll (output, NULL, 10);
  g_assert_cmpint (pid, >, 0);
  g_assert_cmpint (kill (pid, 0), ==, -1);
  g_assert_cmpint (errno, ==, ESRCH);
  g_assert_cmpint (waitpid (pid, NULL, WNOHANG), ==, -1);
  g_assert_cmpint (errno, ==, ECHILD);
}

static void
remove_tree (const char *path)
{
  g_autoptr(GDir) dir = NULL;
  const char *name;

  if (!g_file_test (path, G_FILE_TEST_IS_SYMLINK) && (dir = g_dir_open (path, 0, NULL)))
    {
      while ((name = g_dir_read_name (dir)))
        {
          g_autofree char *child = g_build_filename (path, name, NULL);

          remove_tree (child);
        }

      g_rmdir (path);
    }
  else
    {
      g_unlink (path);
    }
}

int
main (int   argc,
      char *argv[])
{
  const char *commands[] = { "eglinfo", "vulkaninfo", "lspci", "lsusb" };
  int ret;

  g_test_init (&argc, &argv, NULL);

  test_root = g_dir_make_tmp ("sysprof-devices-XXXXXX", NULL);
  tools_path = g_build_filename (test_root, "tools", NULL);
  invocations_path = g_build_filename (test_root, "invocations", NULL);
  g_assert_cmpint (g_mkdir (tools_path, 0700), ==, 0);
  g_setenv ("PATH", tools_path, TRUE);

  for (guint i = 0; i < G_N_ELEMENTS (commands); i++)
    {
      g_autofree char *body = g_strdup_printf ("echo '%s'${1:+\" $1\"} >> '%s'\necho %s\n",
                                              commands[i], invocations_path, commands[i]);

      write_tool (commands[i], body);
    }

  dex_init ();

  g_test_add_func ("/device-info/inventory", test_inventory);
  g_test_add_func ("/device-info/prepare", test_prepare);
  g_test_add_data_func ("/device-info/command/failure", "failure", test_command);
  g_test_add_data_func ("/device-info/command/missing", "missing", test_command);
  g_test_add_data_func ("/device-info/command/timeout", "timeout", test_command);
  g_test_add_data_func ("/device-info/command/cancel", "cancel", test_command);
  g_test_add_func ("/device-info/command/discard", test_command_discard);

  ret = g_test_run ();

  remove_tree (test_root);
  g_free (test_root);
  g_free (tools_path);
  g_free (invocations_path);

  return ret;
}
