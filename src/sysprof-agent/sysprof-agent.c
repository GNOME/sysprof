/* sysprof-agent.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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
#include <stdlib.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <sysprof.h>

#include "ipc-agent.h"

#define BUFFER_SIZE (4096L*16L) /* 64KB */

static gboolean forward_fd_func (const char  *option_name,
                                 const char  *option_value,
                                 gpointer     data,
                                 GError     **error);

static GMainLoop *main_loop;
static IpcAgent *service;
static int read_fd = -1;
static int write_fd = -1;
static int pty_fd = -1;
static char *directory;
static char *capture_filename;
static char *power_profile;
static GArray *forward_fds;
static char **env;
static SysprofRecording *active_recording;
static gboolean clear_env;
static gboolean aid_battery;
static gboolean aid_compositor;
static gboolean aid_cpu;
static gboolean aid_disk;
static gboolean aid_energy;
static gboolean aid_gjs;
static gboolean aid_memory;
static gboolean aid_memprof;
static gboolean aid_net;
static gboolean aid_perf;
static gboolean aid_tracefd;
static gboolean no_throttle;
static gboolean decode;
static gboolean do_system_bus;
static gboolean do_session_bus;
static gboolean scheduler_details;
static const GOptionEntry options[] = {
  { "read-fd", 0, 0, G_OPTION_ARG_INT, &read_fd, "The read side of the FD to use for D-Bus" },
  { "write-fd", 0, 0, G_OPTION_ARG_INT, &write_fd, "The write side of the FD to use for D-Bus" },
  { "forward-fd", 0, 0, G_OPTION_ARG_CALLBACK, forward_fd_func, "The FD to forward to the subprocess" },
  { "directory", 0, 0, G_OPTION_ARG_FILENAME, &directory, "The directory to run spawn the subprocess from", "PATH" },
  { "capture", 0, 0, G_OPTION_ARG_FILENAME, &capture_filename, "The filename to save the sysprof capture to", "PATH" },
  { "clear-env", 0, 0, G_OPTION_ARG_NONE, &clear_env, "Clear environment instead of inheriting" },
  { "decode", 0, 0, G_OPTION_ARG_NONE, &decode, "Decode symbols at the end of the recording" },
  { "env", 0, 0, G_OPTION_ARG_STRING_ARRAY, &env, "Add an environment variable to the spawned process", "KEY=VALUE" },
  { "cpu", 0, 0, G_OPTION_ARG_NONE, &aid_cpu, "Track CPU usage and frequency" },
  { "gjs", 0, 0, G_OPTION_ARG_NONE, &aid_gjs, "Record stack traces within GJS" },
  { "perf", 0, 0, G_OPTION_ARG_NONE, &aid_perf, "Record stack traces with perf" },
  { "memory", 0, 0, G_OPTION_ARG_NONE, &aid_memory, "Record basic system memory usage" },
  { "memprof", 0, 0, G_OPTION_ARG_NONE, &aid_memprof, "Record stack traces during memory allocations" },
  { "disk", 0, 0, G_OPTION_ARG_NONE, &aid_disk, "Record disk usage information" },
  { "net", 0, 0, G_OPTION_ARG_NONE, &aid_net, "Record network usage information" },
  { "energy", 0, 0, G_OPTION_ARG_NONE, &aid_energy, "Record energy usage using RAPL" },
  { "battery", 0, 0, G_OPTION_ARG_NONE, &aid_battery, "Record battery charge and discharge rates" },
  { "compositor", 0, 0, G_OPTION_ARG_NONE, &aid_compositor, "Record GNOME Shell compositor information" },
  { "no-throttle", 0, 0, G_OPTION_ARG_NONE, &no_throttle, "Disable CPU throttling [Deprecated for --power-profile]" },
  { "tracefd", 0, 0, G_OPTION_ARG_NONE, &aid_tracefd, "Provide TRACEFD to subprocess" },
  { "power-profile", 0, 0, G_OPTION_ARG_STRING, &power_profile, "Use POWER_PROFILE for duration of recording", "power-saver|balanced|performance" },
  { "scheduler", 0, 0, G_OPTION_ARG_NONE, &scheduler_details, "Track when processes are scheduled per CPU" },
  { "session-bus", 0, 0, G_OPTION_ARG_NONE, &do_session_bus, "Profile the D-Bus session bus" },
  { "system-bus", 0, 0, G_OPTION_ARG_NONE, &do_system_bus, "Profile the D-Bus system bus" },
  { NULL }
};

G_GNUC_PRINTF (1, 2)
static void
message (const char *format,
         ...)
{
  g_autofree char *formatted = NULL;
  va_list args;

  if (service == NULL)
    return;

  va_start (args, format);
  formatted = g_strdup_vprintf (format, args);
  va_end (args);

  ipc_agent_emit_log (service, formatted);
}

#define IPC_TYPE_AGENT_IMPL (ipc_agent_impl_get_type())
G_DECLARE_FINAL_TYPE (IpcAgentImpl, ipc_agent_impl, IPC, SYPSROF_IMPL, IpcAgentSkeleton)

struct _IpcAgentImpl
{
  IpcAgentSkeleton parent_instance;
};

static gboolean
handle_force_exit (IpcAgent            *sysprof,
                   GDBusMethodInvocation *invocation)
{
  GSubprocess *subprocess;

  if (active_recording &&
      (subprocess = sysprof_recording_get_subprocess (active_recording)))
    g_subprocess_force_exit (subprocess);

  ipc_agent_complete_force_exit (sysprof, invocation);

  return TRUE;
}

static gboolean
handle_send_signal (IpcAgent            *sysprof,
                    GDBusMethodInvocation *invocation,
                    int                    signum)
{
  GSubprocess *subprocess;

  if (active_recording &&
      (subprocess = sysprof_recording_get_subprocess (active_recording)))
    g_subprocess_send_signal (subprocess, signum);

  ipc_agent_complete_send_signal (sysprof, invocation);

  return TRUE;
}

static void
service_iface_init (IpcAgentIface *iface)
{
  iface->handle_force_exit = handle_force_exit;
  iface->handle_send_signal = handle_send_signal;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (IpcAgentImpl, ipc_agent_impl, IPC_TYPE_AGENT_SKELETON,
                               G_IMPLEMENT_INTERFACE (IPC_TYPE_AGENT, service_iface_init))

static void
ipc_agent_impl_class_init (IpcAgentImplClass *klass)
{
}

static void
ipc_agent_impl_init (IpcAgentImpl *self)
{
}

static gboolean
forward_fd_func (const char  *option_name,
                 const char  *option_value,
                 gpointer     data,
                 GError     **error)
{
  int fd;

  if (forward_fds == NULL)
    forward_fds = g_array_new (FALSE, FALSE, sizeof (int));

  errno = 0;

  if (!(fd = atoi (option_value)) && errno != 0)
    {
      int errsv = errno;
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errsv),
                   "--forward-fd must contain a file-descriptor: %s",
                   g_strerror (errsv));
      return FALSE;
    }

  if (fd < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVAL,
                   "--forward-fd must be 0 or a positive integer");
      return FALSE;
    }

  g_array_append_val (forward_fds, fd);

  return TRUE;
}

static void
split_argv (int     argc,
            char  **argv,
            int    *our_argc,
            char ***our_argv,
            int    *sub_argc,
            char ***sub_argv)
{
  gboolean found_split = FALSE;

  *our_argc = 0;
  *our_argv = g_new0 (char *, 1);

  *sub_argc = 0;
  *sub_argv = g_new0 (char *, 1);

  for (int i = 0; i < argc; i++)
    {
      if (g_strcmp0 (argv[i], "--") == 0)
        {
          found_split = TRUE;
        }
      else if (found_split)
        {
          (*sub_argv) = g_realloc_n (*sub_argv, *sub_argc + 2, sizeof (char *));
          (*sub_argv)[*sub_argc] = g_strdup (argv[i]);
          (*sub_argv)[*sub_argc+1] = NULL;
          (*sub_argc)++;
        }
      else
        {
          (*our_argv) = g_realloc_n (*our_argv, *our_argc + 2, sizeof (char *));
          (*our_argv)[*our_argc] = g_strdup (argv[i]);
          (*our_argv)[*our_argc+1] = NULL;
          (*our_argc)++;
        }
    }
}

static void
warn_error (GError **error)
{
  if (*error)
    {
      g_warning ("%s", (*error)->message);
      g_clear_error (error);
    }
}

static GDBusConnection *
create_connection (GIOStream  *stream,
                   GError    **error)
{
  GDBusConnection *ret;

  g_assert (G_IS_IO_STREAM (stream));
  g_assert (main_loop != NULL);
  g_assert (error != NULL);

  if ((ret = g_dbus_connection_new_sync (stream, NULL,
                                          G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
                                          NULL, NULL, error)))
    {
      g_dbus_connection_set_exit_on_close (ret, FALSE);
      g_signal_connect_swapped (ret, "closed", G_CALLBACK (g_main_loop_quit), main_loop);
    }

  return ret;
}

static void
sysprof_agent_recording_diagnostics_items_changed_cb (GListModel *model,
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

      message ("%s: %s",
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
      sysprof_recording_stop_async (active_recording, NULL, NULL, NULL);
    }

  count++;

  return G_SOURCE_CONTINUE;
}

static void
sysprof_agent_wait_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  SysprofRecording *recording = (SysprofRecording *)object;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (user_data == NULL);

  if (!sysprof_recording_wait_finish (recording, result, &error))
    g_error ("Failed to complete recording: %s", error->message);

  g_main_loop_quit (main_loop);
}

static void
sysprof_agent_record_cb (GObject      *object,
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
                    G_CALLBACK (sysprof_agent_recording_diagnostics_items_changed_cb),
                    NULL);
  sysprof_agent_recording_diagnostics_items_changed_cb (diagnostics,
                                                        0,
                                                        0,
                                                        g_list_model_get_n_items (diagnostics),
                                                        NULL);

  sysprof_recording_wait_async (recording,
                                NULL,
                                sysprof_agent_wait_cb,
                                NULL);

  g_set_object (&active_recording, recording);
}

int
main (int   argc,
      char *argv[])
{
  SysprofCaptureWriter *writer = NULL;
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GDBusConnection) system_bus = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) our_argv = NULL;
  g_auto(GStrv) sub_argv = NULL;
  g_autofd int gjs_trace_fd = -1;
  g_autofd int trace_fd = -1;
  GSubprocess *subprocess;
  int our_argc = -1;
  int sub_argc = -1;

  sysprof_clock_init ();

  g_set_prgname ("sysprof-agent");
  g_set_application_name ("sysprof-agent");

  /* Ignore SIGPIPE as we're using pipes to IPC */
  signal (SIGPIPE, SIG_IGN);

  /* Split argv into pre/post -- command split */
  split_argv (argc, argv, &our_argc, &our_argv, &sub_argc, &sub_argv);
  g_assert (our_argc >= 0);
  g_assert (sub_argc >= 0);

  /* Parse command line options pre -- */
  context = g_option_context_new ("-- COMMAND");
  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &our_argc, &our_argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  /* Make sure we have a filename to capture to */
  if (capture_filename == NULL)
    {
      g_printerr ("You must provide --capture=PATH\n");
      return EXIT_FAILURE;
    }

  /* Setup main loop, we'll need it going forward for things
   * like async D-Bus, waiting for child processes, etc.
   */
  main_loop = g_main_loop_new (NULL, FALSE);

  /* First spin up our bus connections */
  if (!(session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error)))
    warn_error (&error);
  if (!(system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL)))
    warn_error (&error);

  /* Now setup our private p2p D-Bus connection to the controller */
  if (read_fd != -1 || write_fd != -1)
    {
      g_autoptr(GIOStream) stream = NULL;
      g_autoptr(GInputStream) in_stream = NULL;
      g_autoptr(GOutputStream) out_stream = NULL;

      /* Both must be set, not just one side */
      if (read_fd == -1 || write_fd == -1)
        {
          g_printerr ("You must specify both --read-fd and --write-fd\n");
          return EXIT_FAILURE;
        }

      /* We need these FDs non-blocking for async IO */
      if (!g_unix_set_fd_nonblocking (read_fd, TRUE, &error) ||
          !g_unix_set_fd_nonblocking (write_fd, TRUE, &error))
        {
          g_printerr ("Failed to set FDs in nonblocking mode: %s\n",
                      error->message);
          return EXIT_FAILURE;
        }

      /* Create stream using FDs provided to us */
      in_stream = g_unix_input_stream_new (read_fd, FALSE);
      out_stream = g_unix_output_stream_new (write_fd, FALSE);
      stream = g_simple_io_stream_new (in_stream, out_stream);

      /* Create connection using our private stream from the controller */
      if (!(connection = create_connection (stream, &error)))
        {
          g_printerr ("Failed to setup P2P D-Bus connection: %s\n",
                      error->message);
          return EXIT_FAILURE;
        }

      /* Now export our service at "/" (but don't start processing messages
       * until we start the profiler, further on.
       */
      service = g_object_new (IPC_TYPE_AGENT_IMPL, NULL);
      if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (service),
                                             connection, "/", &error))
        {
          g_printerr ("Failed to export service over D-Bus connection: %s",
                      error->message);
          return EXIT_FAILURE;
        }
    }

  /* Now start setting up our profiler */
  profiler = sysprof_profiler_new ();

  if (aid_battery)
    sysprof_profiler_add_instrument (profiler, sysprof_battery_charge_new ());

  if (aid_cpu)
    sysprof_profiler_add_instrument (profiler, sysprof_cpu_usage_new ());

  if (aid_disk)
    sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());

  if (aid_energy)
    sysprof_profiler_add_instrument (profiler, sysprof_energy_usage_new ());

  if (aid_memory)
    sysprof_profiler_add_instrument (profiler, sysprof_memory_usage_new ());

  if (aid_net)
    sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

  if (aid_perf)
    sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());

  sysprof_profiler_add_instrument (profiler, sysprof_system_logs_new ());

  if (power_profile)
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new (power_profile));
  else if (no_throttle)
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new ("performance"));

  if (aid_compositor)
    sysprof_profiler_add_instrument (profiler,
                                     sysprof_proxied_instrument_new (G_BUS_TYPE_SESSION,
                                                                     "org.gnome.Shell",
                                                                     "/org/gnome/Sysprof3/Profiler"));

  if (do_session_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SESSION));

  if (do_system_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SYSTEM));

  if (scheduler_details)
    sysprof_profiler_add_instrument (profiler, sysprof_scheduler_details_new ());

  if (decode)
    sysprof_profiler_add_instrument (profiler, sysprof_symbols_bundle_new ());

  /* If -- was ommitted or there are no commands, just profile the entire
   * system without spawning anything. Really only useful when testing the
   * agent without a D-Bus service.
   */
  if (sub_argc >= 0)
    {
      g_autoptr(SysprofSpawnable) spawnable = sysprof_spawnable_new ();
      g_auto(GStrv) current_env = g_get_environ ();

      if (directory != NULL)
        sysprof_spawnable_set_cwd (spawnable, directory);
      else
        sysprof_spawnable_set_cwd (spawnable, g_get_current_dir ());

      sysprof_spawnable_append_args (spawnable, (const char * const *)sub_argv);

      if (clear_env)
        sysprof_spawnable_set_environ (spawnable, NULL);
      else
        sysprof_spawnable_set_environ (spawnable, (const char * const *)current_env);

      if (env != NULL)
        {
          for (guint i = 0; env[i]; i++)
            {
              g_autofree char *key = NULL;
              const char *eq;

              if (!(eq = strchr (env[i], '=')))
                {
                  sysprof_spawnable_setenv (spawnable, env[i], "");
                  continue;
                }

              key = g_strndup (env[i], eq - env[i]);
              sysprof_spawnable_setenv (spawnable, key, eq+1);
            }
        }

      sysprof_profiler_set_spawnable (profiler, spawnable);

      if (aid_gjs)
        gjs_trace_fd = sysprof_spawnable_add_trace_fd (spawnable, "GJS_TRACE_FD");

      if (aid_tracefd)
        trace_fd = sysprof_spawnable_add_trace_fd (spawnable, NULL);

      if (aid_memprof)
        sysprof_spawnable_add_ld_preload (spawnable, PACKAGE_LIBDIR"/libsysprof-memory-"API_VERSION_S".so");

      if (forward_fds != NULL)
        {
          for (guint f = 0; f < forward_fds->len; f++)
            {
              int fd = g_array_index (forward_fds, int, f);
              sysprof_spawnable_take_fd (spawnable, dup (fd), fd);
            }
        }

      if (pty_fd != -1)
        {
          sysprof_spawnable_take_fd (spawnable, dup (pty_fd), STDIN_FILENO);
          sysprof_spawnable_take_fd (spawnable, dup (pty_fd), STDOUT_FILENO);
          sysprof_spawnable_take_fd (spawnable, dup (pty_fd), STDERR_FILENO);
        }
    }

  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (g_steal_fd (&gjs_trace_fd)));
  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (g_steal_fd (&trace_fd)));

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

  /* Now open the writer for our session */
  if (!(writer = sysprof_capture_writer_new (capture_filename, BUFFER_SIZE)))
    {
      int errsv = errno;
      g_printerr ("Failed to open capture writer: %s\n",
                  g_strerror (errsv));
      return EXIT_FAILURE;
    }

  sysprof_profiler_record_async (profiler,
                                 writer,
                                 NULL,
                                 sysprof_agent_record_cb,
                                 NULL);

  g_unix_signal_add (SIGINT, sigint_handler, main_loop);
  g_unix_signal_add (SIGTERM, sigint_handler, main_loop);

  /* Now tell the connection to start processing messages that are
   * delivered from the controller, or signals destined back.
   */
  if (connection != NULL)
    g_dbus_connection_start_message_processing (connection);

  g_main_loop_run (main_loop);

  sysprof_capture_writer_flush (writer);
  sysprof_capture_writer_unref (writer);

  subprocess = sysprof_recording_get_subprocess (active_recording);

  /* Try to exit the same way as the subprocess did to propagate that
   * back into Builder who is watching *this* process.
   */
  if (subprocess != NULL)
    {
      if (g_subprocess_get_if_signaled (subprocess))
        {
          int signum = g_subprocess_get_term_sig (subprocess);
          /* Try to exit in the same manner, or SIGKILL if that doesn't
           * work, or just EXIT_FAILURE as last resort.
           */
          raise (signum);
          raise (SIGKILL);
          return EXIT_FAILURE;
        }

      if (g_subprocess_get_if_exited (subprocess))
        return g_subprocess_get_exit_status (subprocess);
    }

  return EXIT_SUCCESS;
}
