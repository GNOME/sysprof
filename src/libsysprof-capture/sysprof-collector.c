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
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#ifdef __linux__
# include <sched.h>
#endif
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sysprof-capture-writer.h"
#include "sysprof-collector.h"

struct _SysprofCollector
{
  SysprofCaptureWriter *writer;
  int tid;
  int pid;
};

#ifdef __linux__
# define sysprof_current_cpu (sched_getcpu())
#else
# define sysprof_current_cpu (-1)
#endif

static SysprofCaptureWriter *request_writer         (void);
static void                  sysprof_collector_free (gpointer data);
static SysprofCollector     *sysprof_collector_get  (void);

static GPrivate collector_key = G_PRIVATE_INIT (sysprof_collector_free);

static SysprofCaptureWriter *
request_writer (void)
{
  static G_LOCK_DEFINE (control_fd);
  static GDBusConnection *peer;
  SysprofCaptureWriter *writer = NULL;

  G_LOCK (control_fd);

  if (peer == NULL)
    {
      const gchar *fdstr = g_getenv ("SYSPROF_CONTROL_FD");
      int peer_fd = -1;

      if (fdstr != NULL)
        peer_fd = atoi (fdstr);

      if (peer_fd > 0)
        {
          GInputStream *in_stream;
          GOutputStream *out_stream;
          GIOStream *io_stream;

          g_unix_set_fd_nonblocking (peer_fd, TRUE, NULL);

          in_stream = g_unix_input_stream_new (dup (peer_fd), TRUE);
          out_stream = g_unix_output_stream_new (dup (peer_fd), TRUE);
          io_stream = g_simple_io_stream_new (in_stream, out_stream);
          peer = g_dbus_connection_new_sync (io_stream,
                                             NULL,
                                             G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
                                             NULL,
                                             NULL,
                                             NULL);

          if (peer != NULL)
            {
              g_dbus_connection_set_exit_on_close (peer, FALSE);
              g_dbus_connection_start_message_processing (peer);
            }
          
          g_clear_object (&in_stream);
          g_clear_object (&out_stream);
          g_clear_object (&io_stream);
        }
    }

  if (peer != NULL)
    {
      GUnixFDList *out_fd_list = NULL;
      GVariant *reply;

      reply = g_dbus_connection_call_with_unix_fd_list_sync (peer,
                                                             NULL,
                                                             "/org/gnome/Sysprof3/Collector",
                                                             "org.gnome.Sysprof3.Collector",
                                                             "CreateWriter",
                                                             g_variant_new ("()"),
                                                             G_VARIANT_TYPE ("(h)"),
                                                             G_DBUS_CALL_FLAGS_NONE,
                                                             -1,
                                                             NULL,
                                                             &out_fd_list,
                                                             NULL,
                                                             NULL);

      if (reply != NULL)
        {
          int fd = g_unix_fd_list_get (out_fd_list, 0, NULL);

          if (fd > -1)
            writer = sysprof_capture_writer_new_from_fd (fd, 0);

          g_variant_unref (reply);
        }
    }

  G_UNLOCK (control_fd);

  return g_steal_pointer (&writer);
}

static void
sysprof_collector_free (gpointer data)
{
  SysprofCollector *collector = data;

  if (collector != NULL)
    {
      if (collector->writer != NULL)
        sysprof_capture_writer_flush (collector->writer);
      g_clear_pointer (&collector->writer, sysprof_capture_writer_unref);
    }
}

static SysprofCollector *
sysprof_collector_get (void)
{
  SysprofCollector *collector = g_private_get (&collector_key);

  if G_UNLIKELY (collector == NULL)
    {
      collector = g_slice_new0 (SysprofCollector);
      collector->writer = request_writer ();
      collector->pid = getpid ();
#ifdef __linux__
      collector->tid = syscall (__NR_gettid, 0);
#endif
      g_private_replace (&collector_key, collector);
    }

  return collector;
}

SysprofCollector *
sysprof_collector_get_thread_default (void)
{
  return sysprof_collector_get ();
}

void
sysprof_collector_embed_file (SysprofCollector *collector,
                              const gchar      *path,
                              const guint8     *data,
                              gsize             data_len)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_file (collector->writer,
                                     SYSPROF_CAPTURE_CURRENT_TIME,
                                     sysprof_current_cpu,
                                     collector->pid,
                                     path,
                                     TRUE,
                                     data,
                                     data_len);
}

void
sysprof_collector_embed_file_fd (SysprofCollector *collector,
                                 const gchar      *path,
                                 int               fd)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_file_fd (collector->writer,
                                        SYSPROF_CAPTURE_CURRENT_TIME,
                                        sysprof_current_cpu,
                                        collector->pid,
                                        path,
                                        fd);
}

void
sysprof_collector_mark (SysprofCollector *collector,
                        gint64            time,
                        guint64           duration,
                        const gchar      *group,
                        const gchar      *name,
                        const gchar      *message)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_mark (collector->writer,
                                     SYSPROF_CAPTURE_CURRENT_TIME,
                                     sysprof_current_cpu,
                                     collector->pid,
                                     duration,
                                     group,
                                     name,
                                     message);
}

void
sysprof_collector_set_metadata (SysprofCollector *collector,
                                const gchar      *id,
                                const gchar      *value,
                                gssize            value_len)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_metadata (collector->writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         sysprof_current_cpu,
                                         collector->pid,
                                         id,
                                         value,
                                         value_len);
}

void
sysprof_collector_sample (SysprofCollector            *collector,
                          gint64                       time,
                          const SysprofCaptureAddress *addrs,
                          guint                        n_addrs)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_sample (collector->writer,
                                       time,
                                       sysprof_current_cpu,
                                       collector->pid,
                                       collector->tid,
                                       addrs,
                                       n_addrs);
}

void
sysprof_collector_log (SysprofCollector *collector,
                       GLogLevelFlags    severity,
                       const gchar      *domain,
                       const gchar      *message)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_log (collector->writer,
                                    SYSPROF_CAPTURE_CURRENT_TIME,
                                    sysprof_current_cpu,
                                    collector->pid,
                                    severity,
                                    domain,
                                    message);
}

SysprofCaptureAddress
sysprof_collector_map_jitted_ip (SysprofCollector *collector,
                                 const gchar      *name)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    return sysprof_capture_writer_add_jitmap (collector->writer, name);

  return 0;
}

void
sysprof_collector_allocate (SysprofCollector      *collector,
                            SysprofCaptureAddress  alloc_addr,
                            gint64                 alloc_size,
                            SysprofBacktraceFunc   backtrace_func,
                            gpointer               backtrace_data)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_add_allocation (collector->writer,
                                           SYSPROF_CAPTURE_CURRENT_TIME,
                                           sysprof_current_cpu,
                                           collector->pid,
                                           collector->tid,
                                           alloc_addr,
                                           alloc_size,
                                           backtrace_func,
                                           backtrace_data);
}

guint
sysprof_collector_reserve_counters (SysprofCollector *collector,
                                    guint             n_counters)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    return sysprof_capture_writer_request_counter (collector->writer,
                                                   n_counters);

  return 1;
}

void
sysprof_collector_define_counters (SysprofCollector            *collector,
                                   const SysprofCaptureCounter *counters,
                                   guint                        n_counters)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_define_counters (collector->writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            sysprof_current_cpu,
                                            collector->pid,
                                            counters,
                                            n_counters);
}

void
sysprof_collector_publish_counters (SysprofCollector                 *collector,
                                    const guint                      *counters_ids,
                                    const SysprofCaptureCounterValue *values,
                                    guint                             n_counters)
{
  if (collector == NULL)
    collector = sysprof_collector_get ();

  if (collector->writer != NULL)
    sysprof_capture_writer_set_counters (collector->writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         sysprof_current_cpu,
                                         collector->pid,
                                         counters_ids,
                                         values,
                                         n_counters);
}
