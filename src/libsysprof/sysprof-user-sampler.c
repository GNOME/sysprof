/* sysprof-user-sampler.c
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

#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/eventfd.h>

#ifdef HAVE_PERF_REGS_H
# include <asm/perf_regs.h>
#endif

#include <glib/gstdio.h>

#include "sysprof-instrument-private.h"
#include "sysprof-perf-event-stream-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-user-sampler.h"
#include "sysprof-muxer-source.h"
#include "sysprof-fd-private.h"

#include "ipc-unwinder.h"

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
# ifdef PERF_REG_EXTENDED_MASK
#  define SYSPROF_ARCH_PREFERRED_REGS PERF_REG_EXTENDED_MASK
# else
#  define SYSPROF_ARCH_PREFERRED_REGS 0
# endif
#endif /* _ASM_{arch}_PERF_REGS_H */

#define N_WAKEUP_EVENTS 149

struct _SysprofUserSampler
{
  SysprofInstrument parent_instance;
  GArray *perf_fds;
  int capture_fd_read;
  int capture_fd_write;
  int event_fd;
  guint stack_size;
};

struct _SysprofUserSamplerClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofUserSampler, sysprof_user_sampler, SYSPROF_TYPE_INSTRUMENT)

static void
close_fd (gpointer data)
{
  int *fdp = data;

  if (*fdp != -1)
    {
      close (*fdp);
      *fdp = -1;
    }
}

static void
promise_resolve_fd (DexPromise *promise,
                    int         fd)
{
  GValue gvalue = {SYSPROF_TYPE_FD, {{.v_pointer = &fd}, {.v_int = 0}}};
  dex_promise_resolve (promise, &gvalue);
}

static int
await_fd (DexFuture  *future,
          GError    **error)
{
  SysprofFD *fd = dex_await_boxed (future, error);
  int ret = -1;

  if (fd != NULL)
    {
      ret = sysprof_fd_steal (fd);
      sysprof_fd_free (fd);
    }

  return ret;
}

static void
sysprof_user_sampler_ioctl (SysprofUserSampler *self,
                            gboolean            enable)
{
  for (guint i = 0; i < self->perf_fds->len; i++)
    {
      int perf_fd = g_array_index (self->perf_fds, int, i);

      if (0 != ioctl (perf_fd, enable ? PERF_EVENT_IOC_ENABLE : PERF_EVENT_IOC_DISABLE))
        {
          int errsv = errno;
          g_warning ("Failed to toggle perf_fd: %s", g_strerror (errsv));
        }
    }
}

static char **
sysprof_user_sampler_list_required_policy (SysprofInstrument *instrument)
{
  static const char *policy[] = {"org.gnome.sysprof3.profile", NULL};

  return g_strdupv ((char **)policy);
}

typedef struct _Prepare
{
  SysprofRecording *recording;
  SysprofUserSampler *sampler;
  guint stack_size;
} Prepare;

static void
prepare_free (Prepare *prepare)
{
  g_clear_object (&prepare->recording);
  g_clear_object (&prepare->sampler);
  g_free (prepare);
}

static void
_perf_event_open_cb (GObject      *object,
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
      g_autofd int fd = -1;
      int handle;

      g_variant_get (ret, "(h)", &handle);

      if (-1 == (fd = g_unix_fd_list_get (fd_list, handle, &error)))
        goto failure;

      promise_resolve_fd (promise, g_steal_fd (&fd));
      return;
    }

failure:
  dex_promise_reject (promise, g_steal_pointer (&error));
}

static int
_perf_event_open (GDBusConnection  *connection,
                  int               cpu,
                  guint             stack_size,
                  GError          **error)
{
  g_autoptr(DexPromise) promise = NULL;
  g_autoptr(GVariant) options = NULL;
  g_autofd int perf_fd = -1;
  struct perf_event_attr attr = {0};
  gboolean with_mmap2 = TRUE;
  gboolean use_software = FALSE;

  g_assert (G_IS_DBUS_CONNECTION (connection));

try_again:
  attr.sample_type = PERF_SAMPLE_IP
                   | PERF_SAMPLE_TID
                   | PERF_SAMPLE_IDENTIFIER
                   | PERF_SAMPLE_CALLCHAIN
                   | PERF_SAMPLE_STACK_USER
                   | PERF_SAMPLE_REGS_USER
                   | PERF_SAMPLE_TIME;
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

  attr.sample_stack_user = stack_size;
  attr.sample_regs_user = SYSPROF_ARCH_PREFERRED_REGS;

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

  options = _sysprof_perf_event_attr_to_variant (&attr);
  promise = dex_promise_new ();

  g_dbus_connection_call_with_unix_fd_list (connection,
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
                                            G_DBUS_CALL_FLAGS_NONE,
                                            G_MAXUINT,
                                            NULL,
                                            NULL,
                                            _perf_event_open_cb,
                                            dex_ref (promise));

  if (-1 == (perf_fd = await_fd (dex_ref (promise), error)))
    {
      g_clear_pointer (&options, g_variant_unref);

      if (with_mmap2)
        {
          with_mmap2 = FALSE;
          goto try_again;
        }

      if (use_software == FALSE)
        {
          with_mmap2 = TRUE;
          use_software = TRUE;
          goto try_again;
        }

      return -1;
    }

  return g_steal_fd (&perf_fd);
}

static void
call_unwind_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GUnixFDList) out_fd_list = NULL;
  GError *error = NULL;

  g_assert (IPC_IS_UNWINDER (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (ipc_unwinder_call_unwind_finish (IPC_UNWINDER (object), &out_fd_list, result, &error))
    dex_promise_resolve_boolean (promise, TRUE);
  else
    dex_promise_reject (promise, error);
}

static void
create_unwinder_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  IpcUnwinder *unwinder;
  GError *error = NULL;

  if ((unwinder = ipc_unwinder_proxy_new_finish (result, &error)))
    dex_promise_resolve_object (promise, unwinder);
  else
    dex_promise_reject (promise, error);
}

static IpcUnwinder *
create_unwinder (GDBusConnection  *connection,
                 GError          **error)
{
  g_autoptr(DexPromise) promise = dex_promise_new ();
  ipc_unwinder_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE,
                          "org.gnome.Sysprof3",
                          "/org/gnome/Sysprof3/Unwinder",
                          NULL,
                          create_unwinder_cb,
                          dex_ref (promise));
  return dex_await_object (dex_ref (promise), error);
}

static DexFuture *
sysprof_user_sampler_prepare_fiber (gpointer user_data)
{
  Prepare *prepare = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GError) error = NULL;
  GVariantBuilder builder;
  gboolean all_failed = TRUE;
  guint n_cpu;

  g_assert (prepare != NULL);
  g_assert (SYSPROF_IS_RECORDING (prepare->recording));
  g_assert (SYSPROF_IS_USER_SAMPLER (prepare->sampler));

  if (!(connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  if (!dex_await (_sysprof_recording_add_file (prepare->recording,
                                               "/proc/kallsyms",
                                               TRUE),
                  &error))
    {
      _sysprof_recording_diagnostic (prepare->recording,
                                     "Sampler",
                                     "Failed to record copy of “kallsyms” to capture: %s",
                                     error->message);
      g_clear_error (&error);
    }

  n_cpu = g_get_num_processors ();
  fd_list = g_unix_fd_list_new ();

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(hi)"));

  for (guint i = 0; i < n_cpu; i++)
    {
      g_autofd int fd = _perf_event_open (connection, i, prepare->stack_size, &error);

      if (fd == -1)
        {
          _sysprof_recording_diagnostic (prepare->recording,
                                         "Sampler",
                                         "Failed to load Perf event stream for CPU %d with stack size %u: %s",
                                         i, prepare->stack_size, error->message);
          g_clear_error (&error);
        }
      else
        {
          int handle = g_unix_fd_list_append (fd_list, fd, &error);

          if (handle == -1)
            {
              _sysprof_recording_diagnostic (prepare->recording,
                                             "Sampler",
                                             "Out of FDs to add to FDList: %s",
                                             error->message);
              g_clear_error (&error);
            }
          else
            {
              g_array_append_val (prepare->sampler->perf_fds, fd);
              fd = -1;

              g_variant_builder_add (&builder, "(hi)", handle, i);

              all_failed = FALSE;
            }
        }
    }

  if (!all_failed)
    {
      g_autoptr(IpcUnwinder) unwinder = create_unwinder (connection, &error);

      if (unwinder == NULL)
        {
          _sysprof_recording_diagnostic (prepare->recording,
                                         "Sampler",
                                         "Failed to locate unwinder service: %s",
                                         error->message);
          g_clear_error (&error);
        }
      else
        {
          g_autoptr(DexPromise) promise = dex_promise_new ();
          int event_fd_handle = g_unix_fd_list_append (fd_list, prepare->sampler->event_fd, NULL);
          int capture_fd_handle = g_unix_fd_list_append (fd_list, prepare->sampler->capture_fd_write, NULL);

          ipc_unwinder_call_unwind (unwinder,
                                    prepare->stack_size,
                                    g_variant_builder_end (&builder),
                                    g_variant_new_handle (event_fd_handle),
                                    g_variant_new_handle (capture_fd_handle),
                                    fd_list,
                                    NULL,
                                    call_unwind_cb,
                                    dex_ref (promise));

          if (!dex_await (dex_ref (promise), &error))
            {
              _sysprof_recording_diagnostic (prepare->recording,
                                             "Sampler",
                                             "Failed to setup thread unwinder: %s",
                                             error->message);
              g_clear_error (&error);
            }
        }
    }

  g_variant_builder_clear (&builder);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_user_sampler_prepare (SysprofInstrument *instrument,
                              SysprofRecording  *recording)
{
  SysprofUserSampler *self = (SysprofUserSampler *)instrument;
  Prepare *prepare;

  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  prepare = g_new0 (Prepare, 1);
  prepare->recording = g_object_ref (recording);
  prepare->sampler = g_object_ref (self);
  prepare->stack_size = self->stack_size;

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_user_sampler_prepare_fiber,
                              prepare,
                              (GDestroyNotify)prepare_free);
}

typedef struct _Record
{
  SysprofRecording *recording;
  SysprofUserSampler *sampler;
  DexFuture *cancellable;
} Record;

static void
record_free (Record *record)
{
  g_clear_object (&record->recording);
  g_clear_object (&record->sampler);
  dex_clear (&record->cancellable);
  g_free (record);
}

static DexFuture *
sysprof_user_sampler_record_fiber (gpointer user_data)
{
  SysprofCaptureWriter *writer;
  Record *record = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GSource) muxer_source = NULL;
  guint64 exiting = 1234;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_USER_SAMPLER (record->sampler));
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);

  if (record->sampler->capture_fd_read != -1)
    {
      sysprof_user_sampler_ioctl (record->sampler, TRUE);

      g_debug ("Staring muxer for capture_fd %d", record->sampler->capture_fd_read);
      muxer_source = sysprof_muxer_source_new (g_steal_fd (&record->sampler->capture_fd_read), writer);
      g_source_set_static_name (muxer_source, "[stack-muxer]");
      g_source_attach (muxer_source, NULL);

      if (!dex_await (dex_ref (record->cancellable), &error))
        g_debug ("UserSampler shutting down for reason: %s", error->message);

      write (record->sampler->event_fd, &exiting, sizeof exiting);

      g_source_destroy (muxer_source);

      sysprof_user_sampler_ioctl (record->sampler, FALSE);
    }
  else
    g_warning ("No capture FD available for muxing");

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_user_sampler_record (SysprofInstrument *instrument,
                             SysprofRecording  *recording,
                             GCancellable      *cancellable)
{
  SysprofUserSampler *self = (SysprofUserSampler *)instrument;
  Record *record;

  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->sampler = g_object_ref (self);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_user_sampler_record_fiber,
                              record,
                              (GDestroyNotify)record_free);
}

static void
sysprof_user_sampler_finalize (GObject *object)
{
  SysprofUserSampler *self = (SysprofUserSampler *)object;

  g_clear_pointer (&self->perf_fds, g_array_unref);

  g_clear_fd (&self->capture_fd_read, NULL);
  g_clear_fd (&self->capture_fd_write, NULL);
  g_clear_fd (&self->event_fd, NULL);

  G_OBJECT_CLASS (sysprof_user_sampler_parent_class)->finalize (object);
}

static void
sysprof_user_sampler_class_init (SysprofUserSamplerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_user_sampler_finalize;

  instrument_class->list_required_policy = sysprof_user_sampler_list_required_policy;
  instrument_class->prepare = sysprof_user_sampler_prepare;
  instrument_class->record = sysprof_user_sampler_record;
}

static void
sysprof_user_sampler_init (SysprofUserSampler *self)
{
  int fds[2];

  self->event_fd = eventfd (0, EFD_CLOEXEC);

  self->capture_fd_read = -1;
  self->capture_fd_write = -1;

  if (pipe2 (fds, O_CLOEXEC) == 0)
    {
      self->capture_fd_read = fds[0];
      self->capture_fd_write = fds[1];
    }

  self->perf_fds = g_array_new (FALSE, FALSE, sizeof (int));
  g_array_set_clear_func (self->perf_fds, close_fd);
}

SysprofInstrument *
sysprof_user_sampler_new (guint stack_size)
{
  SysprofUserSampler *self;

  g_return_val_if_fail (stack_size > 0, NULL);
  g_return_val_if_fail (stack_size % sysprof_getpagesize () == 0, NULL);

  self = g_object_new (SYSPROF_TYPE_USER_SAMPLER, NULL);
  self->stack_size = stack_size;

  return SYSPROF_INSTRUMENT (self);
}
