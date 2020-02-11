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
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ipc-collector.h"

#include "sysprof-control-source.h"

struct _SysprofControlSource
{
  GObject               parent_instance;
  IpcCollector         *collector;
  GDBusConnection      *conn;
  SysprofCaptureWriter *writer;
  GPtrArray            *files;
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofControlSource, sysprof_control_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static SysprofSourceInterface *parent_iface;

SysprofControlSource *
sysprof_control_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_CONTROL_SOURCE, NULL);
}

static void
sysprof_control_source_finalize (GObject *object)
{
  SysprofControlSource *self = (SysprofControlSource *)object;

  g_clear_object (&self->conn);
  g_clear_object (&self->collector);
  g_clear_pointer (&self->files, g_ptr_array_unref);

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
  self->files = g_ptr_array_new_with_free_func (g_free);
}

static gboolean
on_handle_create_writer_cb (IpcCollector          *collector,
                            GDBusMethodInvocation *invocation,
                            GUnixFDList           *in_fd_list,
                            SysprofControlSource  *self)
{
  gchar writer_tmpl[] = "sysprof-collector-XXXXXX";
  int fd;

  g_assert (IPC_IS_COLLECTOR (collector));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  fd = g_mkstemp_full (writer_tmpl, O_CLOEXEC, 0);

  if (fd > -1)
    {
      g_autoptr(GUnixFDList) out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);

      if (out_fd_list != NULL)
        {
          g_ptr_array_add (self->files, g_strdup (writer_tmpl));
          ipc_collector_complete_create_writer (collector,
                                                g_steal_pointer (&invocation),
                                                out_fd_list,
                                                g_variant_new_handle (0));
          return TRUE;
        }
    }

  g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_FAILED,
                                         "Failed to create FD for writer");

  return TRUE;
}

static void
sysprof_control_source_modify_spawn (SysprofSource    *source,
                                     SysprofSpawnable *spawnable)
{
  SysprofControlSource *self = (SysprofControlSource *)source;
  g_autofree gchar *child_no_str = NULL;
  g_autoptr(GInputStream) in_stream = NULL;
  g_autoptr(GOutputStream) out_stream = NULL;
  g_autoptr(GIOStream) io_stream = NULL;
  g_autoptr(GDBusConnection) conn = NULL;
  int fds[2];
  int child_no;

  g_assert (SYSPROF_IS_SOURCE (source));
  g_assert (SYSPROF_IS_SPAWNABLE (spawnable));

  /* Create a socket pair to communicate D-Bus protocol over */
  if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fds) != 0)
    return;

  g_unix_set_fd_nonblocking (fds[0], TRUE, NULL);
  g_unix_set_fd_nonblocking (fds[1], TRUE, NULL);

  /* @child_no is assigned the FD the child will receive. We can
   * use that to set the environment vaiable of the control FD.
   */
  child_no = sysprof_spawnable_take_fd (spawnable, fds[1], -1);
  child_no_str = g_strdup_printf ("%d", child_no);
  sysprof_spawnable_setenv (spawnable, "SYSPROF_CONTROL_FD", child_no_str);

  /* Create our D-Bus connection for our side and export our service
   * that can create new writers.
   */
  in_stream = g_unix_input_stream_new (dup (fds[0]), TRUE);
  out_stream = g_unix_output_stream_new (fds[0], TRUE);
  io_stream = g_simple_io_stream_new (in_stream, out_stream);
  conn = g_dbus_connection_new_sync (io_stream, NULL,
                                     G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
                                     NULL, NULL, NULL);

  g_set_object (&self->conn, conn);

  self->collector = ipc_collector_skeleton_new ();

  g_signal_connect_object (self->collector,
                           "handle-create-writer",
                           G_CALLBACK (on_handle_create_writer_cb),
                           self,
                           0);

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->collector),
                                    conn,
                                    "/org/gnome/Sysprof3/Collector",
                                    NULL);

  g_dbus_connection_start_message_processing (conn);
}

static void
sysprof_control_source_stop (SysprofSource *source)
{
  SysprofControlSource *self = (SysprofControlSource *)source;

  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  if (self->conn != NULL)
    {
      g_dbus_connection_close_sync (self->conn, NULL, NULL);
      g_clear_object (&self->conn);
    }

  if (parent_iface->stop)
    parent_iface->stop (source);
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
sysprof_control_source_supplement (SysprofSource        *source,
                                   SysprofCaptureReader *reader)
{
  SysprofControlSource *self = (SysprofControlSource *)source;

  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  for (guint i = 0; i < self->files->len; i++)
    {
      const gchar *filename = g_ptr_array_index (self->files, i);
      int fd = open (filename, O_RDONLY);

      if (fd > -1)
        {
          SysprofCaptureReader *worker = sysprof_capture_reader_new_from_fd (fd, NULL);

          if (reader != NULL)
            {
              sysprof_capture_writer_cat (self->writer, worker, NULL);
              sysprof_capture_reader_unref (worker);
            }

          close (fd);
        }
    }
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  parent_iface = g_type_interface_peek_parent (iface);

  iface->set_writer = sysprof_control_source_set_writer;
  iface->modify_spawn = sysprof_control_source_modify_spawn;
  iface->stop = sysprof_control_source_stop;
  iface->supplement = sysprof_control_source_supplement;
}
