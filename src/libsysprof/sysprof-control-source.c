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
  g_autoptr(GError) error = NULL;
  int fd;

  g_assert (IPC_IS_COLLECTOR (collector));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  g_printerr ("Request for new writer\n");

  fd = g_mkstemp_full (writer_tmpl, O_RDWR | O_CLOEXEC, 0640);

  if (fd > -1)
    {
      g_autoptr(GUnixFDList) out_fd_list = g_unix_fd_list_new ();
      gint handle;

      handle = g_unix_fd_list_append (out_fd_list, fd, &error);
      close (fd);

      if (handle > -1)
        {
          g_print ("New fd list with reply %d (fd=%d) %s %p\n", handle, fd, writer_tmpl, out_fd_list);

          g_ptr_array_add (self->files, g_strdup (writer_tmpl));
          ipc_collector_complete_create_writer (collector,
                                                g_steal_pointer (&invocation),
                                                out_fd_list,
                                                g_variant_new_handle (handle));

          g_printerr ("Sent\n");

          return TRUE;
        }
    }

  g_printerr ("Womp sending failure\n");

  if (error != NULL)
    g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
  else
    g_dbus_method_invocation_return_error (g_steal_pointer (&invocation),
                                           G_DBUS_ERROR,
                                           G_DBUS_ERROR_FAILED,
                                           "Failed to create FD for writer");

  return TRUE;
}

static void
on_bus_closed_cb (GDBusConnection      *connection,
                  gboolean              remote_peer_vanished,
                  const GError         *error,
                  SysprofControlSource *self)
{
  if (error != NULL)
    g_warning ("Bus connection prematurely closed: %s\n", error->message);
}

static void
new_connection_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GDBusConnection) conn = NULL;
  g_autoptr(SysprofControlSource) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_CONTROL_SOURCE (self));

  if (!(conn = g_dbus_connection_new_finish (result, &error)))
    {
      sysprof_source_emit_failed (SYSPROF_SOURCE (self), error);
      return;
    }

  g_set_object (&self->conn, conn);

  self->collector = ipc_collector_skeleton_new ();

  g_signal_connect_object (conn,
                           "closed",
                           G_CALLBACK (on_bus_closed_cb),
                           self,
                           0);

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
sysprof_control_source_modify_spawn (SysprofSource    *source,
                                     SysprofSpawnable *spawnable)
{
  SysprofControlSource *self = (SysprofControlSource *)source;
  g_autofree gchar *child_no_str = NULL;
  g_autoptr(GSocketConnection) stream = NULL;
  g_autoptr(GSocket) sock = NULL;
  g_autofree gchar *guid = g_dbus_generate_guid ();
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

  stream = g_socket_connection_factory_create_connection (sock);
  g_dbus_connection_new (G_IO_STREAM (stream),
                         guid,
                         (G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER |
                          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS |
                          G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING),
                         NULL,
                         NULL,
                         new_connection_cb,
                         g_object_ref (self));
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

          if (worker != NULL)
            {
              sysprof_capture_writer_cat (self->writer, worker, NULL);
              sysprof_capture_reader_unref (worker);
            }

          close (fd);
        }

      g_unlink (filename);
    }
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_control_source_set_writer;
  iface->modify_spawn = sysprof_control_source_modify_spawn;
  iface->stop = sysprof_control_source_stop;
  iface->supplement = sysprof_control_source_supplement;
}
