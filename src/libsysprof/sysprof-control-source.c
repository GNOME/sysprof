/* sysprof-control-source.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-control-source"

#include "config.h"

#include <fcntl.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixconnection.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "mapped-ring-buffer.h"
#include "mapped-ring-buffer-source.h"

#include "sysprof-control-source.h"

#define CREATRING      "CreatRing\0"
#define CREATRING_LEN  10

struct _SysprofControlSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  GArray               *source_ids;

#ifdef G_OS_UNIX
  GUnixConnection      *conn;
#endif

  GCancellable         *cancellable;

  /* Control messages are 10 bytes */
  gchar                 read_buf[10];

  guint                 stopped : 1;

};

typedef struct
{
  SysprofControlSource *self;
  guint id;
} RingData;

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofControlSource, sysprof_control_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
ring_data_free (RingData *rd)
{
  g_clear_object (&rd->self);
  g_slice_free (RingData, rd);
}

SysprofControlSource *
sysprof_control_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_CONTROL_SOURCE, NULL);
}

static void
remove_source_id (gpointer data)
{
  guint *id = data;
  g_source_remove (*id);
}

static void
sysprof_control_source_finalize (GObject *object)
{
  SysprofControlSource *self = (SysprofControlSource *)object;

#ifdef G_OS_UNIX
  g_clear_object (&self->conn);
#endif

  if (self->source_ids->len > 0)
    g_array_remove_range (self->source_ids, 0, self->source_ids->len);

  g_clear_pointer (&self->source_ids, g_array_unref);

  G_OBJECT_CLASS (sysprof_control_source_parent_class)->finalize (object);
}

static void
sysprof_control_source_class_init (SysprofControlSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_control_source_finalize;
}

static void
sysprof_control_source_init (SysprofControlSource *self)
{
  self->cancellable = g_cancellable_new ();
  self->source_ids = g_array_new (FALSE, FALSE, sizeof (guint));
  g_array_set_clear_func (self->source_ids, remove_source_id);
}

static bool
event_frame_cb (const void *data,
                size_t     *length,
                void       *user_data)
{
  const SysprofCaptureFrame *fr = data;
  RingData *rd = user_data;

  g_assert (rd != NULL);
  g_assert (SYSPROF_IS_CONTROL_SOURCE (rd->self));
  g_assert (rd->id > 0);

  if G_UNLIKELY (rd->self->writer == NULL ||
                 *length < sizeof *fr ||
                 *length < fr->len ||
                 fr->type >= SYSPROF_CAPTURE_FRAME_LAST)
    goto remove_source;

  _sysprof_capture_writer_add_raw (rd->self->writer, fr);

  *length = fr->len;

  return G_SOURCE_CONTINUE;

remove_source:
  for (guint i = 0; i < rd->self->source_ids->len; i++)
    {
      guint id = g_array_index (rd->self->source_ids, guint, i);

      if (id == rd->id)
        {
          g_array_remove_index (rd->self->source_ids, i);
          break;
        }
    }

  return G_SOURCE_REMOVE;
}

#ifdef G_OS_UNIX
static void
sysprof_control_source_read_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(SysprofControlSource) self = user_data;
  GInputStream *input_stream = (GInputStream *)object;
  gssize ret;

  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_INPUT_STREAM (input_stream));

  ret = g_input_stream_read_finish (G_INPUT_STREAM (input_stream), result, NULL);

  if (ret == sizeof self->read_buf)
    {
      if (memcmp (self->read_buf, CREATRING, CREATRING_LEN) == 0)
        {
          g_autoptr(MappedRingBuffer) buffer = NULL;

          if ((buffer = mapped_ring_buffer_new_reader (0)))
            {
              int fd = mapped_ring_buffer_get_fd (buffer);
              RingData *rd;

              rd = g_slice_new0 (RingData);
              rd->self = g_object_ref (self);
              rd->id = mapped_ring_buffer_create_source_full (buffer,
                                                              event_frame_cb,
                                                              rd,
                                                              (GDestroyNotify)ring_data_free);

              g_array_append_val (self->source_ids, rd->id);
              g_unix_connection_send_fd (self->conn, fd, NULL, NULL);
            }
        }

      if (!g_cancellable_is_cancelled (self->cancellable))
        g_input_stream_read_async (G_INPUT_STREAM (input_stream),
                                   self->read_buf,
                                   sizeof self->read_buf,
                                   G_PRIORITY_HIGH,
                                   self->cancellable,
                                   sysprof_control_source_read_cb,
                                   g_object_ref (self));
    }
}
#endif

static void
sysprof_control_source_modify_spawn (SysprofSource    *source,
                                     SysprofSpawnable *spawnable)
{
#ifdef G_OS_UNIX
  SysprofControlSource *self = (SysprofControlSource *)source;
  g_autofree gchar *child_no_str = NULL;
  g_autoptr(GSocketConnection) stream = NULL;
  g_autoptr(GSocket) sock = NULL;
  GInputStream *input_stream;
  int fds[2];
  int child_no;

  g_assert (SYSPROF_IS_SOURCE (source));
  g_assert (SYSPROF_IS_SPAWNABLE (spawnable));

  /* Create a socket pair to communicate D-Bus protocol over */
  if (socketpair (AF_LOCAL, SOCK_STREAM, 0, fds) != 0)
    return;

  g_unix_set_fd_nonblocking (fds[0], TRUE, NULL);
  g_unix_set_fd_nonblocking (fds[1], TRUE, NULL);

  /* @child_no is assigned the FD the child will receive. We can
   * use that to set the environment vaiable of the control FD.
   */
  child_no = sysprof_spawnable_take_fd (spawnable, fds[1], -1);
  child_no_str = g_strdup_printf ("%d", child_no);
  sysprof_spawnable_setenv (spawnable, "SYSPROF_CONTROL_FD", child_no_str);

  /* We need an IOStream for GDBusConnection to use. Since we need
   * the ability to pass FDs, it must be a GUnixSocketConnection.
   */
  if (!(sock = g_socket_new_from_fd (fds[0], NULL)))
    {
      close (fds[0]);
      g_critical ("Failed to create GSocket");
      return;
    }

  g_socket_set_blocking (sock, FALSE);

  stream = g_socket_connection_factory_create_connection (sock);

  g_assert (G_IS_UNIX_CONNECTION (stream));

  self->conn = g_object_ref (G_UNIX_CONNECTION (stream));

  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (stream));

  g_input_stream_read_async (input_stream,
                             self->read_buf,
                             sizeof self->read_buf,
                             G_PRIORITY_HIGH,
                             self->cancellable,
                             sysprof_control_source_read_cb,
                             g_object_ref (self));
#endif
}

static void
sysprof_control_source_stop (SysprofSource *source)
{
  SysprofControlSource *self = (SysprofControlSource *)source;

  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  self->stopped = TRUE;

  g_cancellable_cancel (self->cancellable);

  if (self->source_ids->len > 0)
    g_array_remove_range (self->source_ids, 0, self->source_ids->len);

  sysprof_source_emit_finished (source);
}

static void
sysprof_control_source_set_writer (SysprofSource        *source,
                                   SysprofCaptureWriter *writer)
{
  SysprofControlSource *self = (SysprofControlSource *)source;

  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  if (writer != NULL)
    self->writer = sysprof_capture_writer_ref (writer);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_control_source_set_writer;
  iface->modify_spawn = sysprof_control_source_modify_spawn;
  iface->stop = sysprof_control_source_stop;
}
