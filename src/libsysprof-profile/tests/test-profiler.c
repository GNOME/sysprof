/* test-profiler.c
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

#include <sysprof-profile.h>

static GMainLoop *main_loop;
static char *capture_file;
static const GOptionEntry entries[] = {
  { "capture", 'c', 0, G_OPTION_ARG_FILENAME, &capture_file, "The file to capture into", "CAPTURE" },
  { 0 }
};

static void
record_cb (GObject      *object,
           GAsyncResult *result,
           gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofRecording) recording = sysprof_profiler_record_finish (SYSPROF_PROFILER (object), result, &error);

  g_assert_no_error (error);
  g_assert_nonnull (recording);
  g_assert_true (SYSPROF_IS_RECORDING (recording));
}

int
main (int       argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- Tests the SysprofProfiler");
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureWriter *writer = NULL;

  main_loop = g_main_loop_new (NULL, FALSE);

  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (capture_file == NULL)
    writer = sysprof_capture_writer_new ("capture.syscap", 0);
  else
    writer = sysprof_capture_writer_new (capture_file, 0);

  profiler = sysprof_profiler_new ();

  sysprof_profiler_record_async (profiler, writer, NULL, record_cb, NULL);

  g_main_loop_run (main_loop);

  g_clear_pointer (&writer, sysprof_capture_writer_unref);

  return 0;
}
