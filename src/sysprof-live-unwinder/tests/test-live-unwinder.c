/*
 * test-live-unwinder.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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
#include <unistd.h>

#include <sys/ioctl.h>

#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libdex.h>

#include <sysprof.h>

#include "sysprof-perf-event-stream-private.h"

#if HAVE_POLKIT_AGENT
# define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
# include <polkit/polkit.h>
# include <polkitagent/polkitagent.h>
#endif

#include <asm/perf_regs.h>

#define N_WAKEUP_EVENTS 149

/* The following was provided to Sysprof by Serhei Makarov as part
 * of the eu-stacktrace prototype work.
 */
#ifdef _ASM_X86_PERF_REGS_H
/* #define SYSPROF_ARCH_PREFERRED_REGS PERF_REG_EXTENDED_MASK -- error on x86_64 due to including segment regs*/
#define REG(R) (1ULL << PERF_REG_X86_ ## R)
#define DWARF_NEEDED_REGS (/* no FLAGS */ REG(IP) | REG(SP) | REG(AX) | REG(CX) | REG(DX) | REG(BX) | REG(SI) | REG(DI) | REG(SP) | REG(BP) | /* no segment regs */ REG(R8) | REG(R9) | REG(R10) | REG(R11) | REG(R12) | REG(R13) | REG(R14) | REG(R15))
/* XXX register ordering is defined in linux arch/x86/include/uapi/asm/perf_regs.h;
   see code in tools/perf/util/intel-pt.c intel_pt_add_gp_regs()
   and note how registers are added in the same order as the perf_regs.h enum */
#define SYSPROF_ARCH_PREFERRED_REGS DWARF_NEEDED_REGS
/* TODO: add other architectures, imitating the linux tools/perf tree */
#else
# define SYSPROF_ARCH_PREFERRED_REGS PERF_REG_EXTENDED_MASK
#endif /* _ASM_{arch}_PERF_REGS_H */

static gboolean sample_stack;
static char *kallsyms = NULL;
static int sample_stack_size = 8192;

static void
open_perf_stream_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GDBusConnection *connection = (GDBusConnection *)object;
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if ((ret = g_dbus_connection_call_with_unix_fd_list_finish (connection, &fd_list, result, &error)))
    {
      int handle;
      int fd;

      g_variant_get (ret, "(h)", &handle);

      if (-1 != (fd = g_unix_fd_list_get (fd_list, handle, &error)))
        {
          dex_promise_resolve_fd (promise, g_steal_fd (&fd));
          return;
        }
    }

  g_assert (error != NULL);

  dex_promise_reject (promise, g_steal_pointer (&error));
}

static int
open_perf_stream (GDBusConnection  *bus,
                  int               cpu,
                  GError          **error)
{
  struct perf_event_attr attr = {0};
  gboolean with_mmap2 = TRUE;
  gboolean use_software = FALSE;

  g_assert (G_IS_DBUS_CONNECTION (bus));
  g_assert (cpu >= 0);
  g_assert (error != NULL);

try_again:
  attr.sample_type = PERF_SAMPLE_IP
                   | PERF_SAMPLE_TID
                   | PERF_SAMPLE_IDENTIFIER
                   | PERF_SAMPLE_CALLCHAIN
                   | PERF_SAMPLE_TIME;

  if (sample_stack)
    {
      attr.sample_type |= (PERF_SAMPLE_REGS_USER | PERF_SAMPLE_STACK_USER);
      attr.sample_stack_user = sample_stack_size;
      attr.sample_regs_user = SYSPROF_ARCH_PREFERRED_REGS;
    }

  attr.wakeup_events = N_WAKEUP_EVENTS;
  attr.disabled = TRUE;
  attr.mmap = TRUE;
  attr.mmap2 = with_mmap2;
  attr.comm = 1;
  attr.task = 1;
  attr.exclude_idle = 1;
  attr.sample_id_all = 1;

#ifdef HAVE_PERF_CLOCKID
  attr.clockid = sysprof_clock;
  attr.use_clockid = 1;
#endif

  attr.size = sizeof attr;

  if (use_software)
    {
      attr.type = PERF_TYPE_SOFTWARE;
      attr.config = PERF_COUNT_SW_CPU_CLOCK;
      attr.sample_period = 1000000;
    }
  else
    {
      attr.type = PERF_TYPE_HARDWARE;
      attr.config = PERF_COUNT_HW_CPU_CYCLES;
      attr.sample_period = 1200000;
    }

  {
    g_autoptr(GVariant) options = _sysprof_perf_event_attr_to_variant (&attr);
    g_autoptr(DexPromise) promise = dex_promise_new ();
    g_autofd int fd = -1;

    g_dbus_connection_call_with_unix_fd_list (bus,
                                              "org.gnome.Sysprof3",
                                              "/org/gnome/Sysprof3",
                                              "org.gnome.Sysprof3.Service",
                                              "PerfEventOpen",
                                              g_variant_new ("(@a{sv}iiht)",
                                                             options,
                                                             -1,
                                                             cpu,
                                                             -1,
                                                             0),
                                              G_VARIANT_TYPE ("(h)"),
                                              G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                              G_MAXUINT,
                                              NULL,
                                              dex_promise_get_cancellable (promise),
                                              open_perf_stream_cb,
                                              dex_ref (promise));

    fd = dex_await_fd (dex_ref (promise), error);

    if (*error == NULL)
      {
        g_printerr ("CPU[%d]: opened perf_event stream as FD %d\n", cpu, fd);
        return g_steal_fd (&fd);
      }

    fd = -1;
  }

  if (with_mmap2)
    {
      g_clear_error (error);
      with_mmap2 = FALSE;
      goto try_again;
    }

  if (use_software == FALSE)
    {
      g_clear_error (error);
      with_mmap2 = TRUE;
      use_software = TRUE;
      goto try_again;
    }

  g_assert (*error != NULL);

  return -1;
}

static DexFuture *
main_fiber (gpointer user_data)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int writer_fd = -1;
  int n_cpu = g_get_num_processors ();
  int next_target_fd = 3;

  /* Get our bus we will use for authorization */
  if (!(bus = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Setup our launcher we'll use to map FDs into the translator */
  launcher = g_subprocess_launcher_new (0);

  /* Setup our argv which will notify the child about where to
   * find the FDs containing perf event streams.
   */
  argv = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (argv, g_build_filename (BUILDDIR, "..", "sysprof-live-unwinder", NULL));

  /* Provide kallsyms from the file provided */
  if (kallsyms != NULL)
    {
      int fd = open (kallsyms, O_RDONLY | O_CLOEXEC);

      if (fd != -1)
        {
          g_subprocess_launcher_take_fd (launcher, fd, next_target_fd);
          g_ptr_array_add (argv, g_strdup_printf ("--kallsyms=%d", next_target_fd++));
        }
    }

  if (sample_stack)
    g_ptr_array_add (argv, g_strdup_printf ("--stack-size=%u", sample_stack_size));

  g_printerr ("sysprof-live-unwinder at %s\n", (const char *)argv->pdata[0]);

  /* First try to open a perf_event stream for as many CPUs as we
   * can before we get complaints from the kernel.
   */
  for (int cpu = 0; cpu < n_cpu; cpu++)
    {
      g_autoptr(GError) cpu_error = NULL;
      g_autofd int perf_fd = open_perf_stream (bus, cpu, &cpu_error);

      if (perf_fd == -1)
        {
          g_printerr ("CPU[%d]: %s\n", cpu, cpu_error->message);
          continue;
        }

      if (0 != ioctl (perf_fd, PERF_EVENT_IOC_ENABLE))
        {
          int errsv = errno;
          g_warning ("Failed to enable perf_fd: %s", g_strerror (errsv));
        }

      g_ptr_array_add (argv, g_strdup_printf ("--perf-fd=%d:%d", next_target_fd, cpu));
      g_subprocess_launcher_take_fd (launcher, g_steal_fd (&perf_fd), next_target_fd);

      next_target_fd++;
    }

  /* Now create a FD for our destination capture. */
  if (-1 == (writer_fd = open ("translated.syscap", O_CREAT|O_RDWR|O_CLOEXEC, 0664)) ||
      ftruncate (writer_fd, 0) != 0)
    return dex_future_new_for_errno (errno);
  g_ptr_array_add (argv, g_strdup_printf ("--capture-fd=%d", next_target_fd));
  g_subprocess_launcher_take_fd (launcher, g_steal_fd (&writer_fd), next_target_fd);
  next_target_fd++;

  /* Null-terminate our argv */
  g_ptr_array_add (argv, NULL);

  /* Spawn our worker process with the perf FDs and writer provided */
  if (!(subprocess = g_subprocess_launcher_spawnv (launcher,
                                                   (const char * const *)argv->pdata,
                                                   &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Now wait for the translation process to complete */
  if (!dex_await_boolean (dex_subprocess_wait_check (subprocess), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_true ();
}

static DexFuture *
finally_cb (DexFuture *future,
            gpointer   user_data)
{
  g_autoptr(GError) error = NULL;
  GMainLoop *main_loop = user_data;

  if (!dex_await (dex_ref (future), &error))
    {
      g_printerr ("Error: %s\n", error->message);
      exit (EXIT_FAILURE);
    }

  g_main_loop_quit (main_loop);

  return NULL;
}

int
main (int   argc,
      char *argv[])
{
#if HAVE_POLKIT_AGENT
  PolkitAgentListener *polkit = NULL;
  PolkitSubject *subject = NULL;
#endif
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(GError) error = NULL;
  DexFuture *future;
  GOptionEntry entries[] = {
    { "sample-stack", 's', 0, G_OPTION_ARG_NONE, &sample_stack, "If the stack should be sampled for user-space unwinding" },
    { "sample-stack-size", 'S', 0, G_OPTION_ARG_INT, &sample_stack_size, "If size of the stack to sample in bytes" },
    { "kallsyms", 'k', 0, G_OPTION_ARG_FILENAME, &kallsyms, "Specify kallsyms for use" },
    { NULL }
  };

  sysprof_clock_init ();
  dex_init ();

  main_loop = g_main_loop_new (NULL, FALSE);
  context = g_option_context_new ("- test sysprof-live-unwinder");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

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

  future = dex_scheduler_spawn (NULL, 0, main_fiber, NULL, NULL);
  future = dex_future_finally (future,
                               finally_cb,
                               g_main_loop_ref (main_loop),
                               (GDestroyNotify) g_main_loop_unref);
  dex_future_disown (future);

  g_main_loop_run (main_loop);

  g_clear_pointer (&kallsyms, g_free);

  return EXIT_SUCCESS;
}
