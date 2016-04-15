/* sysprof-cli.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <signal.h>
#include <sysprof.h>

static GMainLoop  *main_loop;
static SpProfiler *profiler;
static int efd = -1;
static int exit_code = EXIT_SUCCESS;

static void
signal_handler (int signum)
{
  gint64 val = 1;

  write (efd, &val, sizeof val);
}

static gboolean
dispatch (GSource     *source,
          GSourceFunc  callback,
          gpointer     callback_data)
{
  sp_profiler_stop (profiler);
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

static GSourceFuncs source_funcs = {
  NULL, NULL, dispatch, NULL
};

static void
profiler_stopped (SpProfiler *profiler_,
                  GMainLoop  *main_loop_)
{
  g_main_loop_quit (main_loop_);
}

static void
profiler_failed (SpProfiler   *profiler_,
                 const GError *reason,
                 GMainLoop    *main_loop_)
{
  g_assert (SP_IS_PROFILER (profiler_));
  g_assert (reason != NULL);

  g_printerr ("Failure: %s\n", reason->message);
  exit_code = EXIT_FAILURE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  SpCaptureWriter *writer;
  SpSource *source;
  GMainContext *main_context;
  GOptionContext *context;
  const gchar *filename = "capture.syscap";
  GError *error = NULL;
  GSource *gsource;
  gchar *command = NULL;
  gboolean version = FALSE;
  int pid = -1;
  int fd;
  GOptionEntry entries[] = {
    { "pid", 'p', 0, G_OPTION_ARG_INT, &pid, N_("Make sysprof specific to a task"), N_("PID") },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, N_("Run a command and profile the process"), N_("COMMAND") },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, N_("Print the sysprof-cli version and exit") },
    { NULL }
  };

  sp_clock_init ();

  context = g_option_context_new (_("[CAPTURE_FILE] - Sysprof"));
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

  efd = eventfd (0, O_CLOEXEC);

  main_loop = g_main_loop_new (NULL, FALSE);

  gsource = g_source_new (&source_funcs, sizeof (GSource));
  g_source_add_unix_fd (gsource, efd, G_IO_IN);
  g_source_attach (gsource, NULL);

  profiler = sp_local_profiler_new ();

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

  if (-1 == (fd = g_open (filename, O_CREAT | O_RDWR | O_CLOEXEC, 0640)))
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

  writer = sp_capture_writer_new_from_fd (fd, 0);
  sp_profiler_set_writer (profiler, writer);

  source = sp_proc_source_new ();
  sp_profiler_add_source (profiler, source);
  g_object_unref (source);

  source = sp_perf_source_new ();
  sp_profiler_add_source (profiler, source);
  g_object_unref (source);

  source = sp_hostinfo_source_new ();
  sp_profiler_add_source (profiler, source);
  g_object_unref (source);

  if (pid != -1)
    {
      sp_profiler_set_whole_system (profiler, FALSE);
      sp_profiler_add_pid (profiler, pid);
    }

  signal (SIGINT, signal_handler);
  signal (SIGTERM, signal_handler);

  sp_profiler_start (profiler);

  /* Restore the process if we stopped it */
  if (command)
    kill (pid, SIGCONT);

  g_printerr ("Recording, press ^C to exit\n");

  g_main_loop_run (main_loop);

  main_context = g_main_loop_get_context (main_loop);
  while (g_main_context_pending (main_context))
    g_main_context_iteration (main_context, FALSE);

  close (efd);

  g_clear_pointer (&writer, sp_capture_writer_unref);
  g_clear_object (&profiler);
  g_clear_pointer (&main_loop, g_main_loop_unref);
  g_clear_pointer (&context, g_option_context_free);

  return EXIT_SUCCESS;
}
