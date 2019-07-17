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
  int pid = -1;
  int fd;
  int flags;
  GOptionEntry entries[] = {
    { "pid", 'p', 0, G_OPTION_ARG_INT, &pid, N_("Make sysprof specific to a task"), N_("PID") },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, N_("Run a command and profile the process"), N_("COMMAND") },
    { "force", 'f', 0, G_OPTION_ARG_NONE, &force, N_("Force overwrite the capture file") },
    { "no-battery", 0, 0, G_OPTION_ARG_NONE, &no_cpu, N_("Disable recording of battery statistics") },
    { "no-cpu", 0, 0, G_OPTION_ARG_NONE, &no_cpu, N_("Disable recording of CPU statistics") },
    { "no-disk", 0, 0, G_OPTION_ARG_NONE, &no_disk, N_("Disable recording of Disk statistics") },
    { "no-perf", 0, 0, G_OPTION_ARG_NONE, &no_perf, N_("Do not record stacktraces using Linux perf") },
    { "no-decode", 0, 0, G_OPTION_ARG_NONE, &no_decode, N_("Do not append symbol name information from local machine") },
    { "no-memory", 0, 0, G_OPTION_ARG_NONE, &no_memory, N_("Disable recording of memory statistics") },
    { "no-network", 0, 0, G_OPTION_ARG_NONE, &no_network, N_("Disable recording of network statistics") },
    { "use-trace-fd", 0, 0, G_OPTION_ARG_NONE, &use_trace_fd, N_("Set SYSPROF_TRACE_FD environment for subprocess") },
    { "gjs", 0, 0, G_OPTION_ARG_NONE, &gjs, N_("Set GJS_TRACE_FD environment to trace GJS processes") },
    { "gnome-shell", 0, 0, G_OPTION_ARG_NONE, &gnome_shell, N_("Connect to org.gnome.Shell for profiler statistics") },
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

      sysprof_profiler_set_spawn (profiler, TRUE);
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
