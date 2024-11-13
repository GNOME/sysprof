/*
 * main.c
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

#include <stdatomic.h>

#include <sys/mman.h>
#include <sys/resource.h>

#include <glib/gstdio.h>
#include <glib-unix.h>

#include <sysprof.h>

#include "sysprof-live-unwinder.h"
#include "sysprof-perf-event-stream-private.h"

#define CAPTURE_BUFFER_SIZE (4096*16)
#define N_PAGES 32

#define DUMP_BYTES(_n, _b, _l)                                           \
  G_STMT_START {                                                         \
    GString *str, *astr;                                                 \
    gsize _i;                                                            \
    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,                               \
          "         %s = %p [%d]", #_n, _b, (gint)_l);                   \
    str = g_string_sized_new (80);                                       \
    astr = g_string_sized_new (16);                                      \
    for (_i = 0; _i < _l; _i++)                                          \
      {                                                                  \
        if ((_i % 16) == 0)                                              \
          g_string_append_printf (str, "%06x: ", (guint)_i);             \
        g_string_append_printf (str, " %02x", _b[_i]);                   \
                                                                         \
        if (g_ascii_isprint(_b[_i]))                                     \
          g_string_append_printf (astr, " %c", _b[_i]);                  \
        else                                                             \
          g_string_append (astr, " .");                                  \
                                                                         \
        if ((_i % 16) == 15)                                             \
          {                                                              \
            g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,                      \
                   "%s  %s", str->str, astr->str);                       \
            str->str[0] = str->len = 0;                                  \
            astr->str[0] = astr->len = 0;                                \
          }                                                              \
        else if ((_i % 16) == 7)                                         \
          {                                                              \
            g_string_append (str, " ");                                  \
            g_string_append (astr, " ");                                 \
          }                                                              \
      }                                                                  \
                                                                         \
    if (_i != 16)                                                        \
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,                            \
             "%-56s  %s", str->str, astr->str);                          \
                                                                         \
    g_string_free (str, TRUE);                                           \
    g_string_free (astr, TRUE);                                          \
  } G_STMT_END

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

typedef struct _PerfSource
{
  GSource                      gsource;

  SysprofLiveUnwinder         *unwinder;
  SysprofCaptureWriter        *writer;

  guint64                      map_size;
  struct perf_event_mmap_page *map;
  guint8                      *map_data;
  const guint8                *map_data_end;
  guint64                      map_data_size;
  guint64                      tail;

  guint8                      *buffer;

  int                          cpu;
  GPid                         self_pid;

  guint                        stack_size;
} PerfSource;

typedef struct _PerfFDArg
{
  int fd;
  int cpu;
} PerfFDArg;

SYSPROF_ALIGNED_BEGIN(8);
typedef struct _StackRegs
{
  guint64 abi;
  guint64 registers[0];
} StackRegs
SYSPROF_ALIGNED_END(8);

SYSPROF_ALIGNED_BEGIN(8);
typedef struct _StackUser
{
  guint64 size;
  guint8  data[0];
} StackUser
SYSPROF_ALIGNED_END(8);

static GArray *all_perf_fds;

static inline void
realign (gsize *pos,
         gsize  align)
{
  *pos = (*pos + align - 1) & ~(align - 1);
}

static void
handle_event (PerfSource             *source,
              const SysprofPerfEvent *event)
{
  gsize offset;
  gint64 time;

  g_assert (source != NULL);
  g_assert (event != NULL);

  switch (event->header.type)
    {
    case PERF_RECORD_COMM:
      offset = strlen (event->comm.comm) + 1;
      realign (&offset, sizeof (guint64));
      offset += sizeof (GPid) + sizeof (GPid);
      memcpy (&time, event->comm.comm + offset, sizeof time);

      if (event->comm.pid == event->comm.tid)
        {
          sysprof_live_unwinder_seen_process (source->unwinder,
                                              time,
                                              source->cpu,
                                              event->comm.pid,
                                              event->comm.comm);
        }

      break;

    case PERF_RECORD_EXIT:
      /* Ignore fork exits for now */
      if (event->exit.tid != event->exit.pid)
        break;

      sysprof_live_unwinder_process_exited (source->unwinder,
                                            event->exit.time,
                                            source->cpu,
                                            event->exit.pid);

      break;

    case PERF_RECORD_FORK:
      sysprof_live_unwinder_process_forked (source->unwinder,
                                            event->fork.time,
                                            source->cpu,
                                            event->fork.ptid,
                                            event->fork.tid);
      break;

    case PERF_RECORD_LOST:
      {
        char message[64];
        g_snprintf (message, sizeof message,
                    "Lost %"G_GUINT64_FORMAT" samples",
                    event->lost.lost);
        sysprof_capture_writer_add_log (source->writer,
                                        SYSPROF_CAPTURE_CURRENT_TIME,
                                        source->cpu,
                                        -1,
                                        G_LOG_LEVEL_CRITICAL, "Sampler", message);
        break;
      }

    case PERF_RECORD_MMAP:
      offset = strlen (event->mmap.filename) + 1;
      realign (&offset, sizeof (guint64));
      offset += sizeof (GPid) + sizeof (GPid);
      memcpy (&time, event->mmap.filename + offset, sizeof time);

      sysprof_live_unwinder_track_mmap (source->unwinder,
                                        time,
                                        source->cpu,
                                        event->mmap.pid,
                                        event->mmap.addr,
                                        event->mmap.addr + event->mmap.len,
                                        event->mmap.pgoff,
                                        0,
                                        event->mmap.filename,
                                        NULL);

      break;

    case PERF_RECORD_MMAP2:
      offset = strlen (event->mmap2.filename) + 1;
      realign (&offset, sizeof (guint64));
      offset += sizeof (GPid) + sizeof (GPid);
      memcpy (&time, event->mmap2.filename + offset, sizeof time);

      if ((event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID) != 0)
        {
          char build_id[G_N_ELEMENTS (event->mmap2.build_id) * 2 + 1];
          guint len = MIN (G_N_ELEMENTS (event->mmap2.build_id), event->mmap2.build_id_size);

          for (guint i = 0; i < len; i++)
            g_snprintf (&build_id[len*2], 3, "%02x", event->mmap2.build_id[i]);
          build_id[len*2] = 0;

          sysprof_live_unwinder_track_mmap (source->unwinder,
                                            time,
                                            source->cpu,
                                            event->mmap2.pid,
                                            event->mmap2.addr,
                                            event->mmap2.addr + event->mmap2.len,
                                            event->mmap2.pgoff,
                                            event->mmap2.ino,
                                            event->mmap2.filename,
                                            build_id);
        }
      else
        {
          sysprof_live_unwinder_track_mmap (source->unwinder,
                                            time,
                                            source->cpu,
                                            event->mmap2.pid,
                                            event->mmap2.addr,
                                            event->mmap2.addr + event->mmap2.len,
                                            event->mmap2.pgoff,
                                            event->mmap2.ino,
                                            event->mmap2.filename,
                                            NULL);
        }
      break;

    case PERF_RECORD_READ:
      break;

    case PERF_RECORD_SAMPLE:
      {
        const guint8 *endptr = (const guint8 *)event + event->header.size;
        const guint64 *ips = event->callchain.ips;
        int n_ips = event->callchain.n_ips;
        guint64 trace[3];

        /* We always expect PERF_RECORD_SAMPLE to contain a callchain because
         * we need that even if we sample the stack for user-space unwinding.
         * Otherwise we lose the blended stack trace.
         */
        if (n_ips == 0)
          {
            if (event->callchain.header.misc & PERF_RECORD_MISC_KERNEL)
              {
                trace[0] = PERF_CONTEXT_KERNEL;
                trace[1] = event->callchain.ip;
                trace[2] = PERF_CONTEXT_USER;

                ips = trace;
                n_ips = 3;
              }
            else
              {
                trace[0] = PERF_CONTEXT_USER;
                trace[1] = event->callchain.ip;

                ips = trace;
                n_ips = 2;
              }
          }

        if (source->stack_size && source->stack_size < event->header.size)
          {
            guint64 dyn_size = *((const guint64 *)endptr - 1);
            const StackUser *stack_user = (const StackUser *)(endptr - sizeof (guint64) - source->stack_size - sizeof (StackUser));
            const StackRegs *stack_regs = (const StackRegs *)&event->callchain.ips[event->callchain.n_ips];
            guint n_regs = ((const guint8 *)stack_user - (const guint8 *)stack_regs->registers) / sizeof (guint64);

#if 0
            g_print ("n_ips=%u stack_size=%ld dyn_size=%ld abi=%ld n_regs=%d\n",
                     n_ips, stack_user->size, dyn_size, stack_regs->abi, n_regs);
#endif

            sysprof_live_unwinder_process_sampled_with_stack (source->unwinder,
                                                              event->callchain.time,
                                                              source->cpu,
                                                              event->callchain.pid,
                                                              event->callchain.tid,
                                                              ips,
                                                              n_ips,
                                                              stack_user->data,
                                                              stack_user->size,
                                                              dyn_size,
                                                              stack_regs->abi,
                                                              stack_regs->registers,
                                                              n_regs);

            break;
          }

        sysprof_live_unwinder_process_sampled (source->unwinder,
                                               event->callchain.time,
                                               source->cpu,
                                               event->callchain.pid,
                                               event->callchain.tid,
                                               ips,
                                               n_ips);

        break;
      }

    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    default:
      break;
    }
}

static gboolean
perf_source_prepare (GSource *gsource,
                     int     *timeout)
{
  *timeout = 50;
  return FALSE;
}

static gboolean
perf_source_check (GSource *gsource)
{
  PerfSource *source = (PerfSource *)gsource;
  guint64 head;
  guint64 tail;

  atomic_thread_fence (memory_order_acquire);

  tail = source->tail;
  head = source->map->data_head;

  if (head < tail)
    tail = head;

  return (head - tail) >= sizeof (struct perf_event_header);
}

static gboolean
perf_source_dispatch (GSource     *gsource,
                      GSourceFunc  callback,
                      gpointer     user_data)
{
  PerfSource *source = (PerfSource *)gsource;
  guint64 n_bytes = source->map_data_size;
  guint64 mask = n_bytes - 1;
  guint64 head;
  guint64 tail;
  guint us = 0;
  guint them = 0;

  g_assert (source != NULL);

  tail = source->tail;
  head = source->map->data_head;

  atomic_thread_fence (memory_order_acquire);

  if (head < tail)
    tail = head;

  while ((head - tail) >= sizeof (struct perf_event_header))
    {
      const SysprofPerfEvent *event;
      struct perf_event_header *header;
      gboolean is_self = FALSE;

      /* Note that:
       *
       * - perf events are a multiple of 64 bits
       * - the perf event header is 64 bits
       * - the data area is a multiple of 64 bits
       *
       * which means there will always be space for one header, which means we
       * can safely dereference the size field.
       */
      header = (struct perf_event_header *)(gpointer)(source->map_data + (tail & mask));

      if (header->size > head - tail)
        {
          /* The kernel did not generate a complete event.
           * I don't think that can happen, but we may as well
           * be paranoid.
           */
          g_warn_if_reached ();
          break;
        }

      if (source->map_data + (tail & mask) + header->size > source->map_data_end)
        {
          guint8 *b = source->buffer;
          gint n_before;
          gint n_after;

          n_after = (tail & mask) + header->size - n_bytes;
          n_before = header->size - n_after;

          memcpy (b, source->map_data + (tail & mask), n_before);
          memcpy (b + n_before, source->map_data, n_after);

          header = (struct perf_event_header *)(gpointer)b;
        }

      event = (SysprofPerfEvent *)header;

      switch (event->header.type)
        {
        default:
        case PERF_RECORD_COMM:
        case PERF_RECORD_EXIT:
        case PERF_RECORD_FORK:
        case PERF_RECORD_MMAP:
        case PERF_RECORD_MMAP2:
          break;

        case PERF_RECORD_SAMPLE:
          is_self = event->callchain.pid == source->self_pid;
          break;

        case PERF_RECORD_READ:
        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
          goto skip_callback;

        case PERF_RECORD_LOST:
          break;
        }

      handle_event (source, event);

      us += is_self;
      them += !is_self;

    skip_callback:
      tail += header->size;
    }

  source->tail = tail;

  atomic_thread_fence (memory_order_seq_cst);

  source->map->data_tail = tail;

  sysprof_capture_writer_flush (source->writer);

  return G_SOURCE_CONTINUE;
}

static void
perf_source_finalize (GSource *gsource)
{
  PerfSource *source = (PerfSource *)gsource;

  if (source->map != NULL &&
      (gpointer)source->map != MAP_FAILED)
    munmap ((gpointer)source->map, source->map_size);

  g_clear_pointer (&source->buffer, g_free);
  g_clear_pointer (&source->writer, sysprof_capture_writer_unref);
  g_clear_object (&source->unwinder);

  source->map = NULL;
  source->map_data = NULL;
  source->map_data_end = NULL;
  source->map_data_size = 0;
  source->map_size = 0;
  source->tail = 0;
}

static const GSourceFuncs source_funcs = {
  .prepare = perf_source_prepare,
  .check = perf_source_check,
  .dispatch = perf_source_dispatch,
  .finalize = perf_source_finalize,
};

static gboolean
perf_source_init (PerfSource            *source,
                  int                    fd,
                  SysprofLiveUnwinder   *unwinder,
                  SysprofCaptureWriter  *writer,
                  int                    cpu,
                  int                    stack_size,
                  GError               **error)
{
  gsize map_size;
  guint8 *map;

  g_assert (source != NULL);
  g_assert (writer != NULL);
  g_assert (SYSPROF_IS_LIVE_UNWINDER (unwinder));
  g_assert (fd > STDERR_FILENO);

  map_size = N_PAGES * sysprof_getpagesize () + sysprof_getpagesize ();
  map = mmap (NULL, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

  if ((gpointer)map == MAP_FAILED)
    {
      int errsv = errno;
      g_set_error_literal (error,
                           G_FILE_ERROR,
                           g_file_error_from_errno (errsv),
                           g_strerror (errsv));
      return FALSE;
    }

  source->writer = sysprof_capture_writer_ref (writer);
  source->unwinder = g_object_ref (unwinder);
  source->buffer = g_malloc (map_size);
  source->map_size = map_size;
  source->map = (gpointer)map;
  source->map_data = map + sysprof_getpagesize ();
  source->map_data_size = N_PAGES * sysprof_getpagesize ();
  source->map_data_end = source->map_data + source->map_data_size;
  source->tail = 0;
  source->self_pid = getpid ();
  source->cpu = cpu;
  source->stack_size = stack_size;

  g_source_add_unix_fd ((GSource *)source, fd, G_IO_IN);

  return TRUE;
}

static GSource *
perf_source_new (int                    perf_fd,
                 SysprofLiveUnwinder   *unwinder,
                 SysprofCaptureWriter  *writer,
                 int                    cpu,
                 int                    stack_size,
                 GError               **error)
{
  GSource *source;

  if (perf_fd <= STDERR_FILENO)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_BADF,
                   "Invalid file-descriptor for perf event stream");
      return NULL;
    }

  source = g_source_new ((GSourceFuncs *)&source_funcs, sizeof (PerfSource));
  g_source_set_static_name (source, "[perf-event-stream]");
  g_source_set_priority (source, G_PRIORITY_HIGH);

  if (!perf_source_init ((PerfSource *)source, perf_fd, unwinder, writer, cpu, stack_size, error))
    g_clear_pointer (&source, g_source_unref);

  return source;
}

static void
clear_perf_fd (gpointer data)
{
  PerfFDArg *arg = data;

  if (arg->fd > STDERR_FILENO)
    {
      close (arg->fd);
      arg->fd = -1;
    }
}

static gboolean
perf_fd_callback (const char  *option_name,
                  const char  *option_value,
                  gpointer     data,
                  GError     **error)
{
  PerfFDArg arg = {-1, -1};

  if (sscanf (option_value, "%d:%d", &arg.fd, &arg.cpu) < 1)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_BADF,
                   "--perf-fd must be in the format FD or FD:CPU_NUMBER");
      return FALSE;
    }

  if (arg.fd <= STDERR_FILENO)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_BADF,
                   "--perf-fd must be >= %d",
                   STDERR_FILENO);
      return FALSE;
    }

  g_array_append_val (all_perf_fds, arg);

  return TRUE;
}

static void
bump_to_max_fd_limit (void)
{
  struct rlimit limit;

  if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
    {
      limit.rlim_cur = limit.rlim_max;

      if (setrlimit (RLIMIT_NOFILE, &limit) != 0)
        g_warning ("Failed to set FD limit to %"G_GSSIZE_FORMAT"",
                    (gssize)limit.rlim_max);
      else
        g_debug ("Set RLIMIT_NOFILE to %"G_GSSIZE_FORMAT"",
                 (gssize)limit.rlim_max);
    }
}

static gboolean
exit_callback (int          fd,
               GIOCondition condition,
               gpointer     user_data)
{
  g_main_loop_quit (user_data);
  return G_SOURCE_REMOVE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofLiveUnwinder) unwinder = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int capture_fd = -1;
  g_autofd int kallsyms_fd = -1;
  g_autofd int event_fd = -1;
  int stack_size = 0;
  const GOptionEntry entries[] = {
    { "perf-fd", 0, 0, G_OPTION_ARG_CALLBACK, perf_fd_callback, "A file-descriptor to the perf event stream", "FD[:CPU]" },
    { "capture-fd", 0, 0, G_OPTION_ARG_INT, &capture_fd, "A file-descriptor to the sysprof capture", "FD" },
    { "event-fd", 0, 0, G_OPTION_ARG_INT, &event_fd, "A file-descriptor to an event-fd used to notify unwinder should exit", "FD" },
    { "kallsyms", 'k', 0, G_OPTION_ARG_INT, &kallsyms_fd, "Bundle kallsyms provided from passed FD", "FD" },
    { "stack-size", 's', 0, G_OPTION_ARG_INT, &stack_size, "Size of stacks being recorded", "STACK_SIZE" },
    { 0 }
  };

  main_loop = g_main_loop_new (NULL, FALSE);

  all_perf_fds = g_array_new (FALSE, FALSE, sizeof (PerfFDArg));
  g_array_set_clear_func (all_perf_fds, clear_perf_fd);

  context = g_option_context_new ("- translate perf event stream to sysprof");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context, "\
  This tool is used by sysprofd to process incoming perf events that\n\
  include a copy of the stack and register state to the Sysprof capture\n\
  format.\n\
\n\
  It should be provided two file-descriptors. One is for the perf-event\n\
  stream and one is for the Sysprof capture writer.\n\
\n\
  Events that are not related to stack traces will also be passed along to\n\
  to the capture in the standard Sysprof capture format. That includes mmap\n\
  events, process information, and more.\n\
\n\
Examples:\n\
\n\
  # FD 3 contains perf_event stream for CPU 1\n\
  sysprof-translate --perf-fd=3:1 --capture-fd=4");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (capture_fd <= STDERR_FILENO)
    {
      g_printerr ("--capture-fd must be > %d\n", STDERR_FILENO);
      return EXIT_FAILURE;
    }

  if (!(writer = sysprof_capture_writer_new_from_fd (g_steal_fd (&capture_fd), CAPTURE_BUFFER_SIZE)))
    {
      int errsv = errno;
      g_printerr ("Failed to create capture writer: %s\n",
                  g_strerror (errsv));
      return EXIT_FAILURE;
    }

  if (all_perf_fds->len == 0)
    {
      g_printerr ("You must secify at least one --perf-fd\n");
      return EXIT_FAILURE;
    }

  bump_to_max_fd_limit ();

  unwinder = sysprof_live_unwinder_new (writer, g_steal_fd (&kallsyms_fd));

  for (guint i = 0; i < all_perf_fds->len; i++)
    {
      const PerfFDArg *arg = &g_array_index (all_perf_fds, PerfFDArg, i);
      g_autoptr(GSource) perf_source = NULL;

      if (!(perf_source = perf_source_new (arg->fd, unwinder, writer, arg->cpu, stack_size, &error)))
        {
          g_printerr ("Failed to initialize perf event stream: %s\n",
                      error->message);
          return EXIT_FAILURE;
        }

      g_source_attach (perf_source, NULL);
    }

  if (event_fd != -1)
    {
      g_autoptr(GSource) exit_source = g_unix_fd_source_new (event_fd, G_IO_IN);

      g_source_set_callback (exit_source,
                             (GSourceFunc)exit_callback,
                             g_main_loop_ref (main_loop),
                             (GDestroyNotify) g_main_loop_unref);
      g_source_attach (exit_source, NULL);
    }

  g_main_loop_run (main_loop);

  g_clear_pointer (&all_perf_fds, g_array_unref);

  return EXIT_SUCCESS;
}
