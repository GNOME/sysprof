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
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <signal.h>
#include <sysprof.h>

#include "sysprof-capture-util-private.h"

static GMainLoop  *main_loop;
static SysprofProfiler *profiler;
static int exit_code = EXIT_SUCCESS;

static gboolean
sigint_handler (gpointer user_data)
{
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

static void
profiler_stopped (SysprofProfiler *profiler_,
                  GMainLoop       *main_loop_)
{
  g_main_loop_quit (main_loop_);
}

static void
profiler_failed (SysprofProfiler *profiler_,
                 const GError    *reason,
                 GMainLoop       *main_loop_)
{
  g_assert (SYSPROF_IS_PROFILER (profiler_));
  g_assert (reason != NULL);

  g_printerr ("Failure: %s\n", reason->message);
  exit_code = EXIT_FAILURE;
  g_main_loop_quit (main_loop_);
}

static gint
merge_files (gint             argc,
             gchar          **argv,
             GOptionContext  *context)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *tmpname = NULL;
  gsize len;
  gint fd;

  if (argc < 3)
    {
      g_autofree gchar *help = NULL;

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

  writer = sysprof_capture_writer_new_from_fd (fd, 4096*4);

  for (guint i = 1; i < argc; i++)
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;
      g_autoptr(GError) error = NULL;

      if (!(reader = sysprof_capture_reader_new (argv[i], &error)))
        {
          g_printerr ("Failed to create reader for \"%s\": %s\n",
                      argv[i], error->message);
          return EXIT_FAILURE;
        }

      if (!sysprof_capture_writer_cat (writer, reader, &error))
        {
          g_printerr ("Failed to join \"%s\": %s\n",
                      argv[i], error->message);
          return EXIT_FAILURE;
        }
    }

  if (g_file_get_contents (tmpname, &contents, &len, NULL))
    {
      const gchar *buf = contents;
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

gint
main (gint   argc,
      gchar *argv[])
{
  g_auto(GStrv) child_argv = NULL;
  PolkitAgentListener *polkit = NULL;
  PolkitSubject *subject = NULL;
  SysprofCaptureWriter *writer;
  SysprofSource *source;
  GMainContext *main_context;
  GOptionContext *context;
  const gchar *filename = "capture.syscap";
  GError *error = NULL;
  gchar *command = NULL;
  gboolean gjs = FALSE;
  gboolean gtk = FALSE;
  gboolean no_battery = FALSE;
  gboolean no_cpu = FALSE;
  gboolean no_decode = FALSE;
  gboolean no_disk = FALSE;
  gboolean no_memory = FALSE;
  gboolean no_network = FALSE;
  gboolean no_perf = FALSE;
  gboolean version = FALSE;
  gboolean force = FALSE;
  gboolean use_trace_fd = FALSE;
  gboolean gnome_shell = FALSE;
  gboolean rapl = FALSE;
  gboolean memprof = FALSE;
  gboolean merge = FALSE;
  int pid = -1;
  int fd;
  int flags;
  GOptionEntry entries[] = {
    { "pid", 'p', 0, G_OPTION_ARG_INT, &pid, N_("Make sysprof specific to a task"), N_("PID") },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, N_("Run a command and profile the process"), N_("COMMAND") },
    { "force", 'f', 0, G_OPTION_ARG_NONE, &force, N_("Force overwrite the capture file") },
    { "no-battery", 0, 0, G_OPTION_ARG_NONE, &no_battery, N_("Disable recording of battery statistics") },
    { "no-cpu", 0, 0, G_OPTION_ARG_NONE, &no_cpu, N_("Disable recording of CPU statistics") },
    { "no-disk", 0, 0, G_OPTION_ARG_NONE, &no_disk, N_("Disable recording of Disk statistics") },
    { "no-perf", 0, 0, G_OPTION_ARG_NONE, &no_perf, N_("Do not record stacktraces using Linux perf") },
    { "no-decode", 0, 0, G_OPTION_ARG_NONE, &no_decode, N_("Do not append symbol name information from local machine") },
    { "no-memory", 0, 0, G_OPTION_ARG_NONE, &no_memory, N_("Disable recording of memory statistics") },
    { "no-network", 0, 0, G_OPTION_ARG_NONE, &no_network, N_("Disable recording of network statistics") },
    { "use-trace-fd", 0, 0, G_OPTION_ARG_NONE, &use_trace_fd, N_("Set SYSPROF_TRACE_FD environment for subprocess") },
    { "gjs", 0, 0, G_OPTION_ARG_NONE, &gjs, N_("Set GJS_TRACE_FD environment to trace GJS processes") },
    { "gtk", 0, 0, G_OPTION_ARG_NONE, &gtk, N_("Set GTK_TRACE_FD environment to trace a GTK application") },
    { "rapl", 0, 0, G_OPTION_ARG_NONE, &rapl, N_("Include RAPL energy statistics") },
    { "memprof", 0, 0, G_OPTION_ARG_NONE, &memprof, N_("Profile memory allocations and frees") },
    { "gnome-shell", 0, 0, G_OPTION_ARG_NONE, &gnome_shell, N_("Connect to org.gnome.Shell for profiler statistics") },
    { "merge", 0, 0, G_OPTION_ARG_NONE, &merge, N_("Merge all provided *.syscap files and write to stdout") },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Print the sysprof-cli version and exit") },
    { NULL }
  };

  sysprof_clock_init ();

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
          child_argv = (gchar **)g_ptr_array_free (ar, FALSE);
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

  /* If merge is set, we aren't recording, but instead merging a bunch of
   * files together into a single syscap.
   */
  if (merge)
    return merge_files (argc, argv, context);

  if (argc > 2)
    {
      gint i;

      g_printerr (_("Too many arguments were passed to sysprof-cli:"));

      for (i = 2; i < argc; i++)
        g_printerr (" %s", argv [i]);
      g_printerr ("\n");

      return EXIT_FAILURE;
    }

  main_loop = g_main_loop_new (NULL, FALSE);

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

  profiler = sysprof_local_profiler_new ();

  g_signal_connect (profiler,
                    "failed",
                    G_CALLBACK (profiler_failed),
                    main_loop);

  g_signal_connect (profiler,
                    "stopped",
                    G_CALLBACK (profiler_stopped),
                    main_loop);

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

  if (command != NULL || child_argv != NULL)
    {
      g_auto(GStrv) env = g_get_environ ();
      g_autofree gchar *cwd = NULL;
      gint child_argc;

      if (child_argv != NULL)
        {
          child_argc = g_strv_length (child_argv);
        }
      else if (command && !g_shell_parse_argv (command, &child_argc, &child_argv, &error))
        {
          g_printerr ("Invalid command: %s\n", error->message);
          return EXIT_FAILURE;
        }

      cwd = g_get_current_dir ();

      sysprof_profiler_set_spawn (profiler, TRUE);
      sysprof_profiler_set_spawn_cwd (profiler, cwd);
      sysprof_profiler_set_spawn_argv (profiler, (const gchar * const *)child_argv);
      sysprof_profiler_set_spawn_env (profiler, (const gchar * const *)env);
    }

  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  sysprof_profiler_set_writer (profiler, writer);

  if (use_trace_fd)
    {
      source = sysprof_tracefd_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (gjs)
    {
      source = sysprof_gjs_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (gtk)
    {
      source = sysprof_tracefd_source_new ();
      sysprof_tracefd_source_set_envvar (SYSPROF_TRACEFD_SOURCE (source), "GTK_TRACE_FD");
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  source = sysprof_proc_source_new ();
  sysprof_profiler_add_source (profiler, source);
  g_object_unref (source);

#ifdef __linux__
  if (!no_perf)
    {
      source = sysprof_perf_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }
#endif

  if (!no_disk)
    {
      source = sysprof_diskstat_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (!no_decode)
    {
      /* Add __symbols__ rollup after recording to avoid loading
       * symbols from the maching viewing the capture.
       */
      source = sysprof_symbols_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (!no_cpu)
    {
      source = sysprof_hostinfo_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (!no_memory)
    {
      source = sysprof_memory_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (!no_battery)
    {
      source = sysprof_battery_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (rapl)
    {
      source = sysprof_proxy_source_new (G_BUS_TYPE_SYSTEM,
                                         "org.gnome.Sysprof3",
                                         "/org/gnome/Sysprof3/RAPL");
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (!no_network)
    {
      source = sysprof_netdev_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (gnome_shell)
    {
      source = sysprof_proxy_source_new (G_BUS_TYPE_SESSION,
                                         "org.gnome.Shell",
                                         "/org/gnome/Sysprof3/Profiler");
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (memprof)
    {
      source = sysprof_memprof_source_new ();
      sysprof_profiler_add_source (profiler, source);
      g_object_unref (source);
    }

  if (pid != -1)
    {
      sysprof_profiler_set_whole_system (profiler, FALSE);
      sysprof_profiler_add_pid (profiler, pid);
    }

  sysprof_profiler_start (profiler);

  g_printerr ("Recording, press ^C to exit\n");

  g_main_loop_run (main_loop);

  main_context = g_main_loop_get_context (main_loop);
  while (g_main_context_pending (main_context))
    g_main_context_iteration (main_context, FALSE);

  sysprof_capture_writer_flush (writer);

  g_clear_pointer (&writer, sysprof_capture_writer_unref);
  g_clear_object (&profiler);
  g_clear_pointer (&main_loop, g_main_loop_unref);
  g_clear_pointer (&context, g_option_context_free);

  return EXIT_SUCCESS;
}
