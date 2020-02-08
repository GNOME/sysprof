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

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "sysprof-control-source.h"

struct _SysprofControlSource
{
  GObject          parent_instance;
  GDBusConnection *conn;
  GPtrArray       *files;
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

  /* TODO */
  //g_dbus_interface_skeleton_export (GDBusInterfaceSkeleton *interface_, GDBusConnection *connection, const gchar *object_path, GError **error)

  g_dbus_connection_start_message_processing (conn);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->modify_spawn = sysprof_control_source_modify_spawn;
}
