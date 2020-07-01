/* sysprof-collector.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
 * irrevocable (except for failure to satisfy the conditions of this license)
 * patent license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer this software, where such license applies only to those
 * patent claims, already acquired or hereafter acquired, licensable by such
 * copyright holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders
 *     and non-copyrightable additions of contributors, in source or binary
 *     form) alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define G_LOG_DOMAIN "sysprof-collector"

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixconnection.h>
#ifdef __linux__
# include <sched.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "mapped-ring-buffer.h"

#include "sysprof-collector.h"

#define MAX_UNWIND_DEPTH 128
#define CREATRING      "CreatRing\0"
#define CREATRING_LEN  10

typedef struct
{
  MappedRingBuffer *buffer;
  bool is_shared;
  int tid;
  int pid;
} SysprofCollector;

#define COLLECTOR_INVALID ((void *)&invalid)

static MappedRingBuffer       *request_writer         (void);
static void                    sysprof_collector_free (void *data);
static const SysprofCollector *sysprof_collector_get  (void);

static G_LOCK_DEFINE (control_fd);
static GPrivate collector_key = G_PRIVATE_INIT (sysprof_collector_free);
static GPrivate single_trace_key = G_PRIVATE_INIT (NULL);
static SysprofCollector *shared_collector;
static SysprofCollector invalid;

static inline bool
use_single_trace (void)
{
  return GPOINTER_TO_INT (g_private_get (&single_trace_key));
}

static inline int
_do_getcpu (void)
{
#ifdef __linux__
  return sched_getcpu ();
#else
  return -1;
#endif
}

static inline size_t
realign (size_t size)
{
  return (size + SYSPROF_CAPTURE_ALIGN - 1) & ~(SYSPROF_CAPTURE_ALIGN - 1);
}

static MappedRingBuffer *
request_writer (void)
{
  static GUnixConnection *conn;
  MappedRingBuffer *buffer = NULL;

  if (conn == NULL)
    {
      const char *fdstr = g_getenv ("SYSPROF_CONTROL_FD");
      int peer_fd = -1;

      if (fdstr != NULL)
        peer_fd = atoi (fdstr);

      g_unsetenv ("SYSPROF_CONTROL_FD");

      if (peer_fd > 0)
        {
          g_autoptr(GSocket) sock = NULL;

          g_unix_set_fd_nonblocking (peer_fd, FALSE, NULL);

          if ((sock = g_socket_new_from_fd (peer_fd, NULL)))
            {
              g_autoptr(GSocketConnection) scon = NULL;

              g_socket_set_blocking (sock, TRUE);

              if ((scon = g_socket_connection_factory_create_connection (sock)) &&
                  G_IS_UNIX_CONNECTION (scon))
                conn = g_object_ref (G_UNIX_CONNECTION (scon));
            }
        }
    }

  if (conn != NULL)
    {
      GOutputStream *out_stream;
      gsize len;

      out_stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));

      if (g_output_stream_write_all (G_OUTPUT_STREAM (out_stream), CREATRING, CREATRING_LEN, &len, NULL, NULL) &&
          len == CREATRING_LEN)
        {
          int ring_fd = g_unix_connection_receive_fd (conn, NULL, NULL);

          if (ring_fd > -1)
            {
              buffer = mapped_ring_buffer_new_writer (ring_fd);
              close (ring_fd);
            }
        }
    }

  return g_steal_pointer (&buffer);
}

static void
write_final_frame (MappedRingBuffer *ring)
{
  SysprofCaptureFrame *fr;

  g_assert (ring != NULL);

  if ((fr = mapped_ring_buffer_allocate (ring, sizeof *fr)))
    {
      fr->len = sizeof *fr; /* aligned */
      fr->type = 0xFF;      /* Invalid */
      fr->cpu = -1;
      fr->pid = -1;
      fr->time = SYSPROF_CAPTURE_CURRENT_TIME;
      mapped_ring_buffer_advance (ring, fr->len);
    }
}

static void
sysprof_collector_free (void *data)
{
  SysprofCollector *collector = data;

  if (collector != NULL && collector != COLLECTOR_INVALID)
    {
      MappedRingBuffer *buffer = g_steal_pointer (&collector->buffer);

      if (buffer != NULL)
        {
          write_final_frame (buffer);
          mapped_ring_buffer_unref (buffer);
        }

      g_free (collector);
    }
}

static const SysprofCollector *
sysprof_collector_get (void)
{
  const SysprofCollector *collector = g_private_get (&collector_key);

  /* We might have gotten here recursively */
  if G_UNLIKELY (collector == COLLECTOR_INVALID)
    return COLLECTOR_INVALID;

  if G_LIKELY (collector != NULL)
    return collector;

  if (use_single_trace () && shared_collector != COLLECTOR_INVALID)
    return shared_collector;

  {
    SysprofCollector *self;

    g_private_replace (&collector_key, COLLECTOR_INVALID);

    G_LOCK (control_fd);

    self = g_new0 (SysprofCollector, 1);
    self->pid = getpid ();
#ifdef __linux__
    self->tid = syscall (__NR_gettid, 0);
#else
    self->tid = self->pid;
#endif

    if (g_getenv ("SYSPROF_CONTROL_FD") != NULL)
      self->buffer = request_writer ();

    if (self->is_shared)
      shared_collector = self;
    else
      g_private_replace (&collector_key, self);

    G_UNLOCK (control_fd);

    return self;
  }
}

void
sysprof_collector_init (void)
{
  static size_t once_init;

  if (g_once_init_enter (&once_init))
    {
      sysprof_clock_init ();
      (void)sysprof_collector_get ();
      g_once_init_leave (&once_init, TRUE);
    }
}

#define COLLECTOR_BEGIN                                           \
  G_STMT_START {                                                  \
    const SysprofCollector *collector = sysprof_collector_get (); \
    if G_LIKELY (collector->buffer)                               \
      {                                                           \
        if G_UNLIKELY (collector->is_shared)                      \
          G_LOCK (control_fd);                                    \
                                                                  \
        {

#define COLLECTOR_END                                             \
        }                                                         \
                                                                  \
        if G_UNLIKELY (collector->is_shared)                      \
          G_UNLOCK (control_fd);                                  \
      }                                                           \
  } G_STMT_END

void
sysprof_collector_allocate (SysprofCaptureAddress   alloc_addr,
                            int64_t                 alloc_size,
                            SysprofBacktraceFunc    backtrace_func,
                            void                   *backtrace_data)
{
  COLLECTOR_BEGIN {
    SysprofCaptureAllocation *ev;
    size_t len;

    len = sizeof *ev + (sizeof (SysprofCaptureAllocation) * MAX_UNWIND_DEPTH);

    if ((ev = mapped_ring_buffer_allocate (collector->buffer, len)))
      {
        int n_addrs;

        /* First take a backtrace, so that backtrace_func() can overwrite
         * a little bit of data *BEFORE* ev->addrs as stratch space. This
         * is useful to allow using unw_backtrace() or backtrace() to skip
         * a small number of frames.
         *
         * We fill in all the other data afterwards which overwrites that
         * scratch space anyway.
         */
        if (backtrace_func)
          n_addrs = backtrace_func (ev->addrs, MAX_UNWIND_DEPTH, backtrace_data);
        else
          n_addrs = 0;

        ev->n_addrs = CLAMP (n_addrs, 0, MAX_UNWIND_DEPTH);
        ev->frame.len = sizeof *ev + sizeof (SysprofCaptureAddress) * ev->n_addrs;
        ev->frame.type = SYSPROF_CAPTURE_FRAME_ALLOCATION;
        ev->frame.cpu = _do_getcpu ();
        ev->frame.pid = collector->pid;
        ev->frame.time = SYSPROF_CAPTURE_CURRENT_TIME;
        ev->tid = collector->tid;
        ev->alloc_addr = alloc_addr;
        ev->alloc_size = alloc_size;
        ev->padding1 = 0;

        mapped_ring_buffer_advance (collector->buffer, ev->frame.len);
      }

  } COLLECTOR_END;
}

void
sysprof_collector_sample (SysprofBacktraceFunc  backtrace_func,
                          void                 *backtrace_data)
{
  COLLECTOR_BEGIN {
    SysprofCaptureSample *ev;
    size_t len;

    len = sizeof *ev + (sizeof (SysprofCaptureSample) * MAX_UNWIND_DEPTH);

    if ((ev = mapped_ring_buffer_allocate (collector->buffer, len)))
      {
        int n_addrs;

        /* See comment from sysprof_collector_allocate(). */
        if (backtrace_func)
          n_addrs = backtrace_func (ev->addrs, MAX_UNWIND_DEPTH, backtrace_data);
        else
          n_addrs = 0;

        ev->n_addrs = CLAMP (n_addrs, 0, MAX_UNWIND_DEPTH);
        ev->frame.len = sizeof *ev + sizeof (SysprofCaptureAddress) * ev->n_addrs;
        ev->frame.type = SYSPROF_CAPTURE_FRAME_SAMPLE;
        ev->frame.cpu = _do_getcpu ();
        ev->frame.pid = collector->pid;
        ev->frame.time = SYSPROF_CAPTURE_CURRENT_TIME;
        ev->tid = collector->tid;
        ev->padding1 = 0;

        mapped_ring_buffer_advance (collector->buffer, ev->frame.len);
      }

  } COLLECTOR_END;
}

void
sysprof_collector_mark (int64_t     time,
                        int64_t     duration,
                        const char *group,
                        const char *mark,
                        const char *message)
{
  COLLECTOR_BEGIN {
    SysprofCaptureMark *ev;
    size_t len;
    size_t sl;

    if (group == NULL)
      group = "";

    if (mark == NULL)
      mark = "";

    if (message == NULL)
      message = "";

    sl = strlen (message);
    len = realign (sizeof *ev + sl + 1);

    if ((ev = mapped_ring_buffer_allocate (collector->buffer, len)))
      {
        ev->frame.len = len;
        ev->frame.type = SYSPROF_CAPTURE_FRAME_MARK;
        ev->frame.cpu = _do_getcpu ();
        ev->frame.pid = collector->pid;
        ev->frame.time = time;
        ev->duration = duration;
        g_strlcpy (ev->group, group, sizeof ev->group);
        g_strlcpy (ev->name, mark, sizeof ev->name);
        memcpy (ev->message, message, sl);
        ev->message[sl] = 0;

        mapped_ring_buffer_advance (collector->buffer, ev->frame.len);
      }

  } COLLECTOR_END;
}

void
sysprof_collector_log (int             severity,
                       const char     *domain,
                       const char     *message)
{
  COLLECTOR_BEGIN {
    SysprofCaptureLog *ev;
    size_t len;
    size_t sl;

    if (domain == NULL)
      domain = "";

    if (message == NULL)
      message = "";

    sl = strlen (message);
    len = realign (sizeof *ev + sl + 1);

    if ((ev = mapped_ring_buffer_allocate (collector->buffer, len)))
      {
        ev->frame.len = len;
        ev->frame.type = SYSPROF_CAPTURE_FRAME_LOG;
        ev->frame.cpu = _do_getcpu ();
        ev->frame.pid = collector->pid;
        ev->frame.time = SYSPROF_CAPTURE_CURRENT_TIME;
        ev->severity = severity & 0xFFFF;
        ev->padding1 = 0;
        ev->padding2 = 0;
        g_strlcpy (ev->domain, domain, sizeof ev->domain);
        memcpy (ev->message, message, sl);
        ev->message[sl] = 0;

        mapped_ring_buffer_advance (collector->buffer, ev->frame.len);
      }

  } COLLECTOR_END;
}

void
sysprof_collector_log_printf (int             severity,
                              const char     *domain,
                              const char     *format,
                              ...)
{
  COLLECTOR_BEGIN {
    g_autofree char *formatted = NULL;
    SysprofCaptureLog *ev;
    va_list args;
    size_t len;
    size_t sl;

    va_start (args, format);
    formatted = g_strdup_vprintf (format, args);
    va_end (args);

    if (domain == NULL)
      domain = "";

    sl = strlen (formatted);
    len = realign (sizeof *ev + sl + 1);

    if ((ev = mapped_ring_buffer_allocate (collector->buffer, len)))
      {
        ev->frame.len = len;
        ev->frame.type = SYSPROF_CAPTURE_FRAME_LOG;
        ev->frame.cpu = _do_getcpu ();
        ev->frame.pid = collector->pid;
        ev->frame.time = SYSPROF_CAPTURE_CURRENT_TIME;
        ev->severity = severity & 0xFFFF;
        ev->padding1 = 0;
        ev->padding2 = 0;
        g_strlcpy (ev->domain, domain, sizeof ev->domain);
        memcpy (ev->message, formatted, sl);
        ev->message[sl] = 0;

        mapped_ring_buffer_advance (collector->buffer, ev->frame.len);
      }
  } COLLECTOR_END;
}
