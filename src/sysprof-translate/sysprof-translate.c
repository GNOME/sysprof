/*
 * sysprof-translate.c
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

#include <glib/gstdio.h>

#include <sysprof.h>

#include "sysprof-perf-event-stream-private.h"

#define CAPTURE_BUFFER_SIZE (4096*16)
#define N_PAGES 32

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

typedef struct _PerfSource
{
  GSource                      gsource;

  SysprofCaptureWriter        *writer;

  gsize                        map_size;
  struct perf_event_mmap_page *map;
  guint8                      *map_data;
  const guint8                *map_data_end;
  guint64                      tail;

  guint8                      *buffer;

  int                          cpu;
  GPid                         self_pid;
} PerfSource;

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
          sysprof_capture_writer_add_process (source->writer,
                                              time,
                                              source->cpu,
                                              event->comm.pid,
                                              event->comm.comm);

#if 0
          _sysprof_recording_follow_process (recording,
                                             event->comm.pid,
                                             event->comm.comm);
#endif
        }

      break;

    case PERF_RECORD_EXIT:
      /* Ignore fork exits for now */
      if (event->exit.tid != event->exit.pid)
        break;

      sysprof_capture_writer_add_exit (source->writer,
                                       event->exit.time,
                                       source->cpu,
                                       event->exit.pid);

      break;

    case PERF_RECORD_FORK:
      sysprof_capture_writer_add_fork (source->writer,
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

      sysprof_capture_writer_add_map (source->writer,
                                      time,
                                      source->cpu,
                                      event->mmap.pid,
                                      event->mmap.addr,
                                      event->mmap.addr + event->mmap.len,
                                      event->mmap.pgoff,
                                      0,
                                      event->mmap.filename);

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

          sysprof_capture_writer_add_map_with_build_id (source->writer,
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
          sysprof_capture_writer_add_map (source->writer,
                                          time,
                                          source->cpu,
                                          event->mmap2.pid,
                                          event->mmap2.addr,
                                          event->mmap2.addr + event->mmap2.len,
                                          event->mmap2.pgoff,
                                          event->mmap2.ino,
                                          event->mmap2.filename);
        }
      break;

    case PERF_RECORD_READ:
      break;

    case PERF_RECORD_SAMPLE:
      {
        const guint64 *ips;
        guint64 trace[3];
        int n_ips;

        ips = event->callchain.ips;
        n_ips = event->callchain.n_ips;

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

        sysprof_capture_writer_add_sample (source->writer,
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
perf_source_dispatch (GSource     *gsource,
                      GSourceFunc  callback,
                      gpointer     user_data)
{
  PerfSource *source = (PerfSource *)gsource;
  guint64 mask = source->map_size - 1;
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
      g_autofree guint8 *free_me = NULL;
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
          break;
        }

      if (source->map_data + (tail & mask) + header->size > source->map_data_end)
        {
          gint n_before;
          gint n_after;

          n_after = (tail & mask) + header->size - source->map_size;
          n_before = header->size - n_after;

          memcpy (source->buffer, source->map_data + (tail & mask), n_before);
          memcpy (source->buffer + n_before, source->map_data, n_after);

          header = (struct perf_event_header *)(gpointer)source->buffer;
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

  source->map = NULL;
  source->map_data = NULL;
  source->map_data_end = NULL;
  source->map_size = 0;
  source->tail = 0;
}

static const GSourceFuncs source_funcs = {
  .dispatch = perf_source_dispatch,
  .finalize = perf_source_finalize,
};

static gboolean
perf_source_init (PerfSource            *source,
                  int                    fd,
                  SysprofCaptureWriter  *writer,
                  int                    cpu,
                  GError               **error)
{
  gsize map_size;
  guint8 *map;

  g_assert (source != NULL);
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
  source->buffer = g_malloc (map_size);
  source->map_size = map_size;
  source->map = (gpointer)map;
  source->map_data = map + sysprof_getpagesize ();
  source->map_data_end = map + map_size;
  source->tail = 0;
  source->self_pid = getpid ();
  source->cpu = cpu;

  g_source_add_unix_fd ((GSource *)source, fd, G_IO_IN);

  return TRUE;
}

static GSource *
perf_source_new (int                    perf_fd,
                 SysprofCaptureWriter  *writer,
                 int                    cpu,
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

  if (!perf_source_init ((PerfSource *)source, perf_fd, writer, cpu, error))
    g_clear_pointer (&source, g_source_unref);

  return source;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(GSource) perf_source = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int perf_fd = -1;
  g_autofd int capture_fd = -1;
  int cpu_id = -1;
  const GOptionEntry entries[] = {
    { "perf-fd", 0, 0, G_OPTION_ARG_INT, &perf_fd, "A file-descriptor to the perf event stream", "FD" },
    { "capture-fd", 0, 0, G_OPTION_ARG_INT, &capture_fd, "A file-descriptor to the sysprof capture", "FD" },
    { "cpu", 0, 0, G_OPTION_ARG_INT, &cpu_id, "The CPU number to use in sample data frames", "N" },
    { 0 }
  };

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
  sysprof-translate --perf-fd=3 --capture-fd=4");

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (perf_fd <= STDERR_FILENO)
    {
      g_printerr ("--perf-fd must be > %d\n", STDERR_FILENO);
      return EXIT_FAILURE;
    }

  if (capture_fd <= STDERR_FILENO)
    {
      g_printerr ("--capture-fd must be > %d\n", STDERR_FILENO);
      return EXIT_FAILURE;
    }

  main_loop = g_main_loop_new (NULL, FALSE);
  writer = sysprof_capture_writer_new_from_fd (g_steal_fd (&capture_fd), CAPTURE_BUFFER_SIZE);

  if (!(perf_source = perf_source_new (perf_fd, writer, cpu_id, &error)))
    {
      g_printerr ("Failed to initialize perf event stream: %s\n",
                  error->message);
      return EXIT_FAILURE;
    }

  g_source_attach (perf_source, NULL);
  g_main_loop_run (main_loop);

  return EXIT_SUCCESS;
}
