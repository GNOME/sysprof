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

#include <glib-unix.h>

#include <sysprof-profile.h>

static GMainLoop *main_loop;
static char *capture_file;
static SysprofRecording *active_recording;
static const GOptionEntry entries[] = {
  { "capture", 'c', 0, G_OPTION_ARG_FILENAME, &capture_file, "The file to capture into", "CAPTURE" },
  { 0 }
};

static void
wait_cb (GObject      *object,
         GAsyncResult *result,
         gpointer      user_data)
{
  SysprofRecording *recording = (SysprofRecording *)object;
  g_autoptr(GError) error = NULL;
  gboolean r;

  r = sysprof_recording_wait_finish (recording, result, &error);
  g_assert_no_error (error);
  g_assert_true (r);

  g_main_loop_quit (main_loop);
}

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

  sysprof_recording_wait_async (recording, NULL, wait_cb, NULL);

  active_recording = recording;
}

static gboolean
sigint_handler (gpointer user_data)
{
  static int count;

  if (count >= 2)
    {
      g_main_loop_quit (main_loop);
      return G_SOURCE_REMOVE;
    }

  g_printerr ("\n");

  if (count == 0)
    {
      g_printerr ("%s\n", "Stopping profiler. Press twice more ^C to force exit.");
      sysprof_recording_stop_async (active_recording, NULL, NULL, NULL);
    }

  count++;

  return G_SOURCE_CONTINUE;
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

  sysprof_profiler_add_instrument (profiler, sysprof_cpu_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

  sysprof_profiler_record_async (profiler, writer, NULL, record_cb, NULL);

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  g_main_loop_run (main_loop);

  g_clear_pointer (&writer, sysprof_capture_writer_unref);

  return 0;
}
