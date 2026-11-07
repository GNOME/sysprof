/*
 * test-no-dbus.c
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

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sysprof-capture.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

static const char *cli_path;
static const char *test_path;

static void
test_no_dbus (void)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *directory = NULL;
  g_autofree char *capture_path = NULL;
  g_autofree char *bus_address = NULL;
  g_autofree char *stderr_buf = NULL;
  SysprofCaptureFrameType type;
  guint processes = 0;
  guint counter_definitions = 0;
  guint counter_values = 0;
  gboolean have_cpuinfo = FALSE;
  gboolean have_mountinfo = FALSE;

  directory = g_dir_make_tmp ("sysprof-no-dbus-XXXXXX", &error);
  g_assert_no_error (error);
  capture_path = g_build_filename (directory, "capture.syscap", NULL);
  bus_address = g_strdup_printf ("unix:path=%s/missing-bus", directory);

  launcher = g_subprocess_launcher_new ((G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                        G_SUBPROCESS_FLAGS_STDERR_PIPE));
  g_subprocess_launcher_setenv (launcher, "DBUS_SYSTEM_BUS_ADDRESS", bus_address, TRUE);
  g_subprocess_launcher_setenv (launcher, "DBUS_SESSION_BUS_ADDRESS", bus_address, TRUE);
  g_subprocess_launcher_setenv (launcher, "G_DEBUG", "fatal-criticals", TRUE);
  g_subprocess_launcher_unsetenv (launcher, "DISPLAY");
  g_subprocess_launcher_unsetenv (launcher, "WAYLAND_DISPLAY");

  /* Keep the normal counters and process/file collection enabled. Perf is
   * disabled because access to perf_event_open() depends on host policy.
   */
  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            cli_path,
                                            "--no-sysprofd",
                                            "--no-perf",
                                            "--no-decode",
                                            capture_path,
                                            "--",
                                            test_path,
                                            "--workload",
                                            NULL);
  g_assert_no_error (error);
  g_assert_true (g_subprocess_communicate_utf8 (subprocess, NULL, NULL,
                                               NULL, &stderr_buf, &error));
  g_assert_no_error (error);
  g_test_message ("sysprof-cli stderr: %s", stderr_buf);
  g_assert_true (g_subprocess_get_successful (subprocess));
  g_assert_null (strstr (stderr_buf, "Failed to complete recording"));

  /* The CLI's exit status alone does not guarantee recording succeeded. */
  reader = sysprof_capture_reader_new (capture_path);
  g_assert_nonnull (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_FILE_CHUNK)
        {
          const SysprofCaptureFileChunk *chunk;

          chunk = sysprof_capture_reader_read_file (reader);
          g_assert_nonnull (chunk);
          have_cpuinfo |= g_str_equal (chunk->path, "/proc/cpuinfo.gz");
          have_mountinfo |= (g_str_has_prefix (chunk->path, "/proc/") &&
                             g_str_has_suffix (chunk->path, "/mountinfo.gz"));
          continue;
        }

      processes += type == SYSPROF_CAPTURE_FRAME_PROCESS;
      counter_definitions += type == SYSPROF_CAPTURE_FRAME_CTRDEF;
      counter_values += type == SYSPROF_CAPTURE_FRAME_CTRSET;
      g_assert_true (sysprof_capture_reader_skip (reader));
    }

  g_assert_cmpuint (processes, >, 0);
  g_assert_cmpuint (counter_definitions, >, 0);
  g_assert_cmpuint (counter_values, >, 0);
  g_assert_true (have_cpuinfo);
  g_assert_true (have_mountinfo);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);
  g_assert_cmpint (g_unlink (capture_path), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  g_autofree char *cli_absolute = NULL;
  g_autofree char *test_absolute = NULL;

  if (argc == 2 && g_str_equal (argv[1], "--workload"))
    {
      g_usleep (1500 * 1000);
      return 0;
    }

  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 2);

  cli_absolute = g_canonicalize_filename (argv[1], NULL);
  test_absolute = g_canonicalize_filename (argv[0], NULL);
  cli_path = cli_absolute;
  test_path = test_absolute;

  g_test_add_func ("/sysprof/cli/no-dbus", test_no_dbus);

  return g_test_run ();
}
