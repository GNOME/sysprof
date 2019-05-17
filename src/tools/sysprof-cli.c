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
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
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
}

gint
main (gint   argc,
      gchar *argv[])
{
  SysprofCaptureWriter *writer;
  SysprofSource *source;
  GMainContext *main_context;
  GOptionContext *context;
  const gchar *filename = "capture.syscap";
  GError *error = NULL;
  gchar *command = NULL;
  gboolean no_memory = FALSE;
  gboolean no_cpu = FALSE;
  gboolean version = FALSE;
  gboolean force = FALSE;
  int pid = -1;
  int fd;
  int flags;
  GOptionEntry entries[] = {
    { "pid", 'p', 0, G_OPTION_ARG_INT, &pid, N_("Make sysprof specific to a task"), N_("PID") },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, N_("Run a command and profile the process"), N_("COMMAND") },
    { "force", 'f', 0, G_OPTION_ARG_NONE, &force, N_("Force overwrite the capture file") },
    { "no-cpu", 0, 0, G_OPTION_ARG_NONE, &no_cpu, N_("Disable recording of CPU statistics") },
    { "no-memory", 0, 0, G_OPTION_ARG_NONE, &no_memory, N_("Disable recording of memory statistics") },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Print the sysprof-cli version and exit") },
    { NULL }
  };

  sysprof_clock_init ();

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  context = g_option_context_new (_("[CAPTURE_FILE] — Sysprof"));
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

  if (command != NULL)
    {
      g_auto(GStrv) child_argv = NULL;
      gint child_argc;

      if (!g_shell_parse_argv (command, &child_argc, &child_argv, &error))
        {
          g_printerr ("Invalid command: %s\n", error->message);
          return EXIT_FAILURE;
        }

      if (!g_spawn_async (NULL, child_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &error))
        {
          g_printerr ("Invalid command: %s\n", error->message);
          return EXIT_FAILURE;
        }

      /* Stop the process until we are setup */
      if (0 != kill (pid, SIGSTOP))
        {
          g_printerr ("Failed to pause inferior during setup\n");
          return EXIT_FAILURE;
        }
    }

  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  sysprof_profiler_set_writer (profiler, writer);

  source = sysprof_proc_source_new ();
  sysprof_profiler_add_source (profiler, source);
  g_object_unref (source);

  source = sysprof_perf_source_new ();
  sysprof_profiler_add_source (profiler, source);
  g_object_unref (source);

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

  if (pid != -1)
    {
      sysprof_profiler_set_whole_system (profiler, FALSE);
      sysprof_profiler_add_pid (profiler, pid);
    }

  sysprof_profiler_start (profiler);

  /* Restore the process if we stopped it */
  if (command)
    kill (pid, SIGCONT);

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
