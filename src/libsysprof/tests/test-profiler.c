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

#include "config.h"

#include <glib-unix.h>
#include <glib/gstdio.h>

#include <sysprof.h>

static GMainLoop *main_loop;
static char *capture_file;
static SysprofRecording *active_recording;
static gboolean memprof;
static gboolean tracer;
static gboolean gnome_shell;
static gboolean bundle_symbols;
static gboolean session_bus;
static gboolean system_bus;
static gboolean gjs;
static char *power_profile;
static const GOptionEntry entries[] = {
  { "capture", 'c', 0, G_OPTION_ARG_FILENAME, &capture_file, "The file to capture into", "CAPTURE" },
  { "memprof", 'm', 0, G_OPTION_ARG_NONE, &memprof, "Do memory allocation tracking on subprocess" },
  { "tracer", 't', 0, G_OPTION_ARG_NONE, &tracer, "Enable tracing with __cyg_profile_enter" },
  { "gjs", 'g', 0, G_OPTION_ARG_NONE, &gjs, "export GJS_TRACE_FD" },
  { "gnome-shell", 's', 0, G_OPTION_ARG_NONE, &gnome_shell, "Request GNOME Shell to provide profiler data" },
  { "power-profile", 'p', 0, G_OPTION_ARG_STRING, &power_profile, "Use POWER_PROFILE for duration of recording", "power-saver|balanced|performance" },
  { "session-bus", 0, 0, G_OPTION_ARG_NONE, &session_bus, "Record D-Bus messages on the session bus" },
  { "system-bus", 0, 0, G_OPTION_ARG_NONE, &system_bus, "Record D-Bus messages on the system bus" },
  { "bundle-symbols", 'b', 0, G_OPTION_ARG_STRING, &bundle_symbols, "Bundle symbols with the capture" },
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
diagnostics_items_changed_cb (GListModel *model,
                              guint       position,
                              guint       removed,
                              guint       added,
                              gpointer    user_data)
{
  for (guint i = 0; i < added; i++)
    {
      g_autoptr(SysprofDiagnostic) diagnostic = g_list_model_get_item (model, position+i);

      g_printerr ("%s: %s\n",
                  sysprof_diagnostic_get_domain (diagnostic),
                  sysprof_diagnostic_get_message (diagnostic));
    }
}

static void
record_cb (GObject      *object,
           GAsyncResult *result,
           gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofRecording) recording = sysprof_profiler_record_finish (SYSPROF_PROFILER (object), result, &error);
  g_autoptr(GListModel) diagnostics = NULL;

  g_assert_no_error (error);
  g_assert_nonnull (recording);
  g_assert_true (SYSPROF_IS_RECORDING (recording));

  diagnostics = sysprof_recording_list_diagnostics (recording);
  g_signal_connect (diagnostics,
                    "items-changed",
                    G_CALLBACK (diagnostics_items_changed_cb),
                    NULL);
  diagnostics_items_changed_cb (diagnostics,
                                0,
                                0,
                                g_list_model_get_n_items (diagnostics),
                                NULL);

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
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("[-- [COMMAND...]]");
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) argv_copy = NULL;
  SysprofCaptureWriter *writer = NULL;
  SysprofCaptureReader *reader = NULL;
  g_autofd int trace_fd = -1;
  g_autofd int gjs_trace_fd = -1;
  int argv_copy_len = 0;

  sysprof_clock_init ();

  main_loop = g_main_loop_new (NULL, FALSE);

  argv_copy = g_new0 (char *, argc+1);
  for (guint i = 0; i < argc; i++)
    {
      if (strcmp ("--", argv[i]) == 0)
        {
          argv_copy[i] = NULL;
          break;
        }

      argv_copy[i] = g_strdup (argv[i]);
      argv_copy_len++;
    }

  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argv_copy_len, &argv_copy, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (capture_file == NULL)
    writer = sysprof_capture_writer_new ("capture.syscap", 0);
  else
    writer = sysprof_capture_writer_new (capture_file, 0);

  profiler = sysprof_profiler_new ();

  sysprof_profiler_add_instrument (profiler, sysprof_battery_charge_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_cpu_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_energy_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_memory_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());
  sysprof_profiler_add_instrument (profiler, sysprof_system_logs_new ());

  if (bundle_symbols)
    sysprof_profiler_add_instrument (profiler, sysprof_symbols_bundle_new ());

  if (power_profile)
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new (power_profile));

  if (gnome_shell)
    sysprof_profiler_add_instrument (profiler,
                                     sysprof_proxied_instrument_new (G_BUS_TYPE_SESSION,
                                                                     "org.gnome.Shell",
                                                                     "/org/gnome/Sysprof3/Profiler"));
  if (memprof)
    sysprof_profiler_add_instrument (profiler, sysprof_malloc_tracing_new ());

  sysprof_profiler_add_instrument (profiler, sysprof_scheduler_details_new ());

  if (tracer)
    sysprof_profiler_add_instrument (profiler, sysprof_tracer_new ());
  else
    sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());

  if (session_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SESSION));

  if (system_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SYSTEM));

  for (int i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--") == 0 && i+1 < argc)
        {
          g_autoptr(SysprofSpawnable) spawnable = sysprof_spawnable_new ();

          sysprof_spawnable_append_args (spawnable, (const char * const *)&argv[i+1]);
          sysprof_spawnable_set_cwd (spawnable, g_get_current_dir ());

          sysprof_profiler_set_spawnable (profiler, spawnable);

          trace_fd = sysprof_spawnable_add_trace_fd (spawnable, NULL);

          if (gjs)
            gjs_trace_fd = sysprof_spawnable_add_trace_fd (spawnable, "GJS_TRACE_FD");

          break;
        }
    }

  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (g_steal_fd (&gjs_trace_fd)));
  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (g_steal_fd (&trace_fd)));

  sysprof_profiler_add_instrument (profiler,
                                   g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                 "stdout-path", "__zoneinfo__",
                                                 "command-argv", (const char * const[]) { "cat", "/proc/zoneinfo", NULL },
                                                 NULL));

  sysprof_profiler_record_async (profiler, writer, NULL, record_cb, NULL);

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  g_main_loop_run (main_loop);

  if (trace_fd != -1)
    {
      if ((reader = sysprof_capture_reader_new_from_fd (g_steal_fd (&trace_fd))))
        sysprof_capture_writer_cat (writer, reader);
    }

  sysprof_capture_writer_flush (writer);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);
  g_clear_pointer (&writer, sysprof_capture_writer_unref);

  return 0;
}
