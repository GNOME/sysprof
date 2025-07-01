/* sysprof-cli.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libdex.h>
#include <sysprof.h>

#if HAVE_POLKIT_AGENT
# define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
# include <polkit/polkit.h>
# include <polkitagent/polkitagent.h>
#endif

#if HAVE_LIBSYSTEMD
# include <systemd/sd-daemon.h>
#endif

#include "sysprof-capture-util-private.h"

#define DEFAULT_BUFFER_SIZE (1024L * 1024L * 8L /* 8mb */)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static gboolean no_decode;
static gboolean disable_debuginfod;
static GMainLoop *main_loop;
static SysprofRecording *active_recording;

static void
diagnostics_items_changed_cb (GListModel *model,
                              guint       position,
                              guint       removed,
                              guint       added,
                              gpointer    user_data)
{
  g_assert (G_IS_LIST_MODEL (model));
  g_assert (user_data == NULL);

  for (guint i = 0; i < added; i++)
    {
      g_autoptr(SysprofDiagnostic) diagnostic = g_list_model_get_item (model, position+i);

      g_printerr ("%s: %s\n",
                  sysprof_diagnostic_get_domain (diagnostic),
                  sysprof_diagnostic_get_message (diagnostic));
    }
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
      if (!no_decode)
        g_printerr ("Decoding symbols and appending to capture.\n");
      sysprof_recording_stop_async (active_recording, NULL, NULL, NULL);
    }

  count++;

  return G_SOURCE_CONTINUE;
}

static int
merge_files (int              argc,
             char           **argv,
             GOptionContext  *context)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *tmpname = NULL;
  gsize len;
  int fd;

  if (argc < 3)
    {
      g_autofree char *help = NULL;

      help = g_option_context_get_help (context, FALSE, NULL);
      g_printerr (_("--merge requires at least 2 filename arguments"));
      g_printerr ("\n\n%s\n", help);

      return EXIT_FAILURE;
    }

  if (isatty (STDOUT_FILENO))
    {
      g_printerr ("stdout is a TTY, refusing to write binary data to stdout.\n");
      return EXIT_FAILURE;
    }

  for (guint i = 1; i < argc; i++)
    {
      if (!g_file_test (argv[i], G_FILE_TEST_IS_REGULAR))
        {
          g_printerr ("\"%s\" is not a regular file.\n", argv[i]);
          return EXIT_FAILURE;
        }
    }

  if (-1 == (fd = g_file_open_tmp (".sysprof-cat-XXXXXX", &tmpname, NULL)))
    {
      g_printerr ("Failed to create memfd for capture file.\n");
      return EXIT_FAILURE;
    }

  writer = sysprof_capture_writer_new_from_fd (fd, 0);

  for (guint i = 1; i < argc; i++)
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;

      if (!(reader = sysprof_capture_reader_new (argv[i])))
        {
          int errsv = errno;
          g_printerr ("Failed to create reader for \"%s\": %s\n",
                      argv[i], g_strerror (errsv));
          return EXIT_FAILURE;
        }

      if (!sysprof_capture_writer_cat (writer, reader))
        {
          int errsv = errno;
          g_printerr ("Failed to join \"%s\": %s\n",
                      argv[i], g_strerror (errsv));
          return EXIT_FAILURE;
        }
    }

  if (g_file_get_contents (tmpname, &contents, &len, NULL))
    {
      const char *buf = contents;
      gsize to_write = len;

      while (to_write > 0)
        {
          gssize n_written;

          n_written = _sysprof_write (STDOUT_FILENO, buf, to_write);

          if (n_written < 0)
            {
              g_printerr ("%s\n", g_strerror (errno));
              g_unlink (tmpname);
              return EXIT_FAILURE;
            }

          buf += n_written;
          to_write -= n_written;
        }
    }

  g_unlink (tmpname);

  return EXIT_SUCCESS;
}

static void
sysprof_cli_wait_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  SysprofRecording *recording = (SysprofRecording *)object;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (user_data == NULL);

  if (!sysprof_recording_wait_finish (recording, result, &error))
    {
      if (error->domain != G_SPAWN_EXIT_ERROR)
        g_printerr ("Failed to complete recording: %s\n",
                    error->message);
    }

  g_main_loop_quit (main_loop);
}

static void
sysprof_cli_record_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  SysprofProfiler *profiler = (SysprofProfiler *)object;
  g_autoptr(SysprofRecording) recording = NULL;
  g_autoptr(GListModel) diagnostics = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (user_data == NULL);

  if (!(recording = sysprof_profiler_record_finish (profiler, result, &error)))
    g_error ("Failed to start profiling session: %s", error->message);

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

  sysprof_recording_wait_async (recording,
                                NULL,
                                sysprof_cli_wait_cb,
                                NULL);

  g_set_object (&active_recording, recording);
}

static void
add_trace_fd (SysprofProfiler  *profiler,
              SysprofSpawnable *spawnable,
              const char       *name)
{
  int trace_fd = -1;

  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (!spawnable || SYSPROF_IS_SPAWNABLE (spawnable));

  if (spawnable == NULL)
    return;

  trace_fd = sysprof_spawnable_add_trace_fd (spawnable, name);
  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (g_steal_fd (&trace_fd)));
}

int
main (int   argc,
      char *argv[])
{
#if HAVE_POLKIT_AGENT
  PolkitAgentListener *polkit = NULL;
  PolkitSubject *subject = NULL;
#endif
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autofree char *power_profile = NULL;
  g_auto(GStrv) child_argv = NULL;
  g_auto(GStrv) envs = NULL;
  g_auto(GStrv) monitor_bus = NULL;
  GMainContext *main_context;
  GOptionContext *context;
  const char *filename = "capture.syscap";
  GError *error = NULL;
  char *command = NULL;
  gboolean gjs = FALSE;
  gboolean gtk = FALSE;
  gboolean no_battery = FALSE;
  gboolean no_cpu = FALSE;
  gboolean no_disk = FALSE;
  gboolean no_memory = FALSE;
  gboolean no_network = FALSE;
  gboolean no_perf = FALSE;
  gboolean no_throttle = FALSE;
  gboolean version = FALSE;
  gboolean force = FALSE;
  gboolean use_trace_fd = FALSE;
  gboolean gnome_shell = FALSE;
  gboolean rapl = FALSE;
  gboolean memprof = FALSE;
  gboolean merge = FALSE;
  gboolean speedtrack = FALSE;
  gboolean scheduler_details = FALSE;
  gboolean system_bus = FALSE;
  gboolean session_bus = FALSE;
  gboolean no_sysprofd = FALSE;
  int stack_size = 0;
  int pid = -1;
  int fd;
  int flags;
  int n_buffer_pages = (DEFAULT_BUFFER_SIZE / sysprof_getpagesize ());
  GOptionEntry entries[] = {
    { "no-throttle", 0, 0, G_OPTION_ARG_NONE, &no_throttle, N_("Disable CPU throttling while profiling [Deprecated for --power-profile]") },
    { "pid", 'p', 0, G_OPTION_ARG_INT, &pid, N_("Make sysprof specific to a task [Deprecated]"), N_("PID") },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, N_("Run a command and profile the process"), N_("COMMAND") },
    { "env", 'e', 0, G_OPTION_ARG_STRING_ARRAY, &envs, N_("Set environment variable for spawned process. Can be used multiple times."), N_("VAR=VALUE") },
    { "force", 'f', 0, G_OPTION_ARG_NONE, &force, N_("Force overwrite the capture file") },
    { "no-battery", 0, 0, G_OPTION_ARG_NONE, &no_battery, N_("Disable recording of battery statistics") },
    { "no-cpu", 0, 0, G_OPTION_ARG_NONE, &no_cpu, N_("Disable recording of CPU statistics") },
    { "no-disk", 0, 0, G_OPTION_ARG_NONE, &no_disk, N_("Disable recording of Disk statistics") },
    { "no-perf", 0, 0, G_OPTION_ARG_NONE, &no_perf, N_("Do not record stacktraces using Linux perf") },
    { "no-decode", 0, 0, G_OPTION_ARG_NONE, &no_decode, N_("Do not append symbol name information from local machine") },
    { "no-memory", 0, 0, G_OPTION_ARG_NONE, &no_memory, N_("Disable recording of memory statistics") },
    { "no-network", 0, 0, G_OPTION_ARG_NONE, &no_network, N_("Disable recording of network statistics") },
    { "use-trace-fd", 0, 0, G_OPTION_ARG_NONE, &use_trace_fd, N_("Set SYSPROF_TRACE_FD environment for subprocess") },
    { "scheduler", 0, 0, G_OPTION_ARG_NONE, &scheduler_details, N_("Track when processes are scheduled") },
    { "session-bus", 0, 0, G_OPTION_ARG_NONE, &session_bus, N_("Profile the D-Bus session bus") },
    { "system-bus", 0, 0, G_OPTION_ARG_NONE, &system_bus, N_("Profile the D-Bus system bus") },
    { "gjs", 0, 0, G_OPTION_ARG_NONE, &gjs, N_("Set GJS_TRACE_FD environment to trace GJS processes") },
    { "gtk", 0, 0, G_OPTION_ARG_NONE, &gtk, N_("Set GTK_TRACE_FD environment to trace a GTK application") },
    { "rapl", 0, 0, G_OPTION_ARG_NONE, &rapl, N_("Include RAPL energy statistics") },
    { "memprof", 0, 0, G_OPTION_ARG_NONE, &memprof, N_("Profile memory allocations and frees") },
    { "gnome-shell", 0, 0, G_OPTION_ARG_NONE, &gnome_shell, N_("Connect to org.gnome.Shell for profiler statistics") },
    { "speedtrack", 0, 0, G_OPTION_ARG_NONE, &speedtrack, N_("Track performance of the applications main loop") },
    { "power-profile", 0, 0, G_OPTION_ARG_STRING, &power_profile, "Use POWER_PROFILE for duration of recording", "power-saver|balanced|performance" },
    { "merge", 0, 0, G_OPTION_ARG_NONE, &merge, N_("Merge all provided *.syscap files and write to stdout") },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Print the sysprof-cli version and exit") },
    { "buffer-size", 0, 0, G_OPTION_ARG_INT, &n_buffer_pages, N_("The size of the buffer in pages (1 = 1 page)") },
    { "monitor-bus", 0, 0, G_OPTION_ARG_STRING_ARRAY, &monitor_bus, N_("Additional D-Bus address to monitor") },
    { "stack-size", 0, 0, G_OPTION_ARG_INT, &stack_size, N_("Stack size to copy for unwinding in user-space") },
    { "no-debuginfod", 0, 0, G_OPTION_ARG_NONE, &disable_debuginfod, N_("Do not use debuginfod to resolve symbols") },
    { "no-sysprofd", 0, 0, G_OPTION_ARG_NONE, &no_sysprofd, N_("Do not use Sysprofd to acquire privileges") },
    { NULL }
  };

  sysprof_clock_init ();
  dex_init ();

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  /* Set up gettext translations */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Before we start processing argv, look for "--" and take everything after
   * it as arguments as a "-c" replacement.
   */
  for (guint i = 1; i < argc; i++)
    {
      if (g_str_equal (argv[i], "--"))
        {
          GPtrArray *ar = g_ptr_array_new ();
          for (guint j = i + 1; j < argc; j++)
            g_ptr_array_add (ar, g_strdup (argv[j]));
          g_ptr_array_add (ar, NULL);
          child_argv = (char **)g_ptr_array_free (ar, FALSE);
          /* Skip everything from -- beyond */
          argc = i;
          break;
        }
    }

  context = g_option_context_new (_("[CAPTURE_FILE] [-- COMMAND ARGS] — Sysprof"));
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

  g_option_context_set_summary (context, N_("\n\
Examples:\n\
\n\
  # Record gtk4-widget-factory using trace-fd to get application provided\n\
  # data as well as GTK and GNOME Shell data providers\n\
  sysprof-cli --gtk --gnome-shell --use-trace-fd -- gtk4-widget-factory\n\
\n\
  # Merge multiple syscap files into one\n\
  sysprof-cli --merge a.syscap b.syscap > c.syscap\n\
\n\
  # Unwind by capturing stack/register contents instead of frame-pointers\n\
  # where the stack-size is a multiple of page-size\n\
  sysprof-cli --stack-size=8192\n\
"));

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (version)
    {
      g_print ("Sysprof "PACKAGE_VERSION"\n");
      return EXIT_SUCCESS;
    }

  if (pid != -1)
    g_printerr ("--pid is no longer supported and will be ignored\n");

  /* If merge is set, we aren't recording, but instead merging a bunch of
   * files together into a single syscap.
   */
  if (merge)
    return merge_files (argc, argv, context);

  if (argc > 2)
    {
      int i;

      g_printerr (_("Too many arguments were passed to sysprof-cli:"));

      for (i = 2; i < argc; i++)
        g_printerr (" %s", argv [i]);
      g_printerr ("\n");

      return EXIT_FAILURE;
    }

  main_loop = g_main_loop_new (NULL, FALSE);

#if HAVE_POLKIT_AGENT
  /* Start polkit agent so that we can elevate privileges from a TTY */
  if (g_getenv ("DESKTOP_SESSION") == NULL &&
      (subject = polkit_unix_process_new_for_owner (getpid (), 0, -1)))
    {
      g_autoptr(GError) pkerror = NULL;

      polkit = polkit_agent_text_listener_new (NULL, NULL);
      polkit_agent_listener_register (polkit,
                                      POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                      subject,
                                      NULL,
                                      NULL,
                                      &pkerror);

      if (pkerror != NULL)
        {
          g_dbus_error_strip_remote_error (pkerror);
          g_printerr ("Failed to register polkit agent: %s\n",
                      pkerror->message);
        }
    }
#endif

  /* Warn about access if we're in a container */
  if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS))
    {
      if (!g_file_test ("/var/run/host/usr", G_FILE_TEST_EXISTS))
        g_printerr ("Warning: Flatpak detected but cannot access host, set --filesystem=host\n");
    }

  profiler = sysprof_profiler_new ();
  sysprof_profiler_set_acquire_privileges (profiler, !no_sysprofd);

  if (argc == 2)
    filename = argv[1];

  flags = O_CREAT | O_RDWR | O_CLOEXEC;
  if (!force)
    {
      /* if we don't force overwrite we want to ensure the file doesn't exist
       * and never overwrite it. O_EXCL will prevent opening in that case */
      flags |= O_EXCL;
      if (g_file_test (filename, G_FILE_TEST_EXISTS))
        {
          /* Translators: %s is a file name. */
          g_printerr (_("%s exists. Use --force to overwrite\n"), filename);
          return EXIT_FAILURE;
        }
    }
  if (-1 == (fd = g_open (filename, flags, 0640)))
    {
      g_printerr ("Failed to open %s\n", filename);
      return EXIT_FAILURE;
    }

  writer = sysprof_capture_writer_new_from_fd (fd, n_buffer_pages * sysprof_getpagesize ());

  if (command != NULL || child_argv != NULL)
    {
      g_autoptr(SysprofSpawnable) spawnable = sysprof_spawnable_new ();
      g_auto(GStrv) current_env = g_get_environ ();
      int child_argc;

      if (child_argv != NULL)
        {
          child_argc = g_strv_length (child_argv);
        }
      else if (command && !g_shell_parse_argv (command, &child_argc, &child_argv, &error))
        {
          g_printerr ("Invalid command: %s\n", error->message);
          return EXIT_FAILURE;
        }

      sysprof_spawnable_set_cwd (spawnable, g_get_current_dir ());
      sysprof_spawnable_append_args (spawnable, (const char * const *)child_argv);
      sysprof_spawnable_set_environ (spawnable, (const char * const *)current_env);

      if (envs != NULL)
        {
          for (guint i = 0; envs[i]; i++)
            {
              g_autofree char *key = NULL;
              const char *eq;

              if (!(eq = strchr (envs[i], '=')))
                {
                  sysprof_spawnable_setenv (spawnable, envs[i], "");
                  continue;
                }

              key = g_strndup (envs[i], eq - envs[i]);
              sysprof_spawnable_setenv (spawnable, key, eq+1);
            }
        }

      sysprof_profiler_set_spawnable (profiler, spawnable);

      if (gjs)
        {
          sysprof_spawnable_setenv (spawnable, "GJS_ENABLE_PROFILER", "1");
          add_trace_fd (profiler, spawnable, "GJS_TRACE_FD");
        }

      if (use_trace_fd)
        add_trace_fd (profiler, spawnable, NULL);

      if (memprof)
        sysprof_spawnable_add_ld_preload (spawnable, PACKAGE_LIBDIR"/libsysprof-memory-"API_VERSION_S".so");

      if (speedtrack)
        sysprof_spawnable_add_ld_preload (spawnable, PACKAGE_LIBDIR"/libsysprof-speedtrack-"API_VERSION_S".so");
    }

  sysprof_profiler_add_instrument (profiler, sysprof_system_logs_new ());

  if (!no_perf)
    {
      if (stack_size == 0)
        sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());
      else
        sysprof_profiler_add_instrument (profiler, sysprof_user_sampler_new (stack_size));
    }

  if (!no_disk)
    sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());

  if (!no_decode)
    {
      SysprofInstrument *bundle = sysprof_symbols_bundle_new ();
      sysprof_symbols_bundle_set_enable_debuginfod (SYSPROF_SYMBOLS_BUNDLE (bundle), !disable_debuginfod);
      sysprof_profiler_add_instrument (profiler, g_steal_pointer (&bundle));
    }

  if (!no_cpu)
    sysprof_profiler_add_instrument (profiler, sysprof_cpu_usage_new ());

  if (!no_memory)
    sysprof_profiler_add_instrument (profiler, sysprof_memory_usage_new ());

  if (!no_battery)
    sysprof_profiler_add_instrument (profiler, sysprof_battery_charge_new ());

  if (rapl)
    sysprof_profiler_add_instrument (profiler, sysprof_energy_usage_new ());

  if (!no_network)
    sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

  if (power_profile)
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new (power_profile));
  else if (no_throttle)
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new ("performance"));

  if (gnome_shell)
    sysprof_profiler_add_instrument (profiler,
                                     sysprof_proxied_instrument_new (G_BUS_TYPE_SESSION,
                                                                     "org.gnome.Shell",
                                                                     "/org/gnome/Sysprof3/Profiler"));

  if (session_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SESSION));

  if (system_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SYSTEM));

  if (scheduler_details)
    sysprof_profiler_add_instrument (profiler, sysprof_scheduler_details_new ());

  if (monitor_bus != NULL)
    {
      for (guint i = 0; monitor_bus[i]; i++)
        sysprof_profiler_add_instrument (profiler,
                                         sysprof_dbus_monitor_new_for_bus_address (monitor_bus[i]));
    }

  sysprof_profiler_add_instrument (profiler,
                                   g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                 "stdout-path", "eglinfo",
                                                 "command-argv", (const char * const[]) {"eglinfo", NULL},
                                                 NULL));
  sysprof_profiler_add_instrument (profiler,
                                   g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                 "stdout-path", "glxinfo",
                                                 "command-argv", (const char * const[]) {"glxinfo", NULL},
                                                 NULL));

  sysprof_profiler_record_async (profiler,
                                 writer,
                                 NULL,
                                 sysprof_cli_record_cb,
                                 NULL);

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  g_printerr ("Recording, press ^C to exit\n");

#if HAVE_LIBSYSTEMD
  if (command == NULL && child_argv == NULL)
    sd_notify (TRUE, "READY=1");
#endif

  g_main_loop_run (main_loop);

  sysprof_capture_writer_flush (writer);

  main_context = g_main_loop_get_context (main_loop);
  while (g_main_context_pending (main_context))
    g_main_context_iteration (main_context, FALSE);

  sysprof_capture_writer_flush (writer);

  return EXIT_SUCCESS;
}
