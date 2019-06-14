/* sysprof-check.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-check"

#include "config.h"

#include "sysprof-check.h"

static void
sysprof_check_supported_ping_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  GDBusConnection *bus = (GDBusConnection *)object;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_DBUS_CONNECTION (bus));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(reply = g_dbus_connection_call_finish (bus, result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_check_supported_bus_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(bus = g_bus_get_finish (result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_dbus_connection_call (bus,
                            "org.gnome.Sysprof3",
                            "/org/gnome/Sysprof3",
                            "org.freedesktop.DBus.Peer",
                            "Ping",
                            g_variant_new ("()"),
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            g_task_get_cancellable (task),
                            sysprof_check_supported_ping_cb,
                            g_object_ref (task));
}

void
sysprof_check_supported_async (GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_check_supported_async);

  /* Get access to the System D-Bus and check to see if we can ping the
   * service that is found at org.gnome.Sysprof3.
   */

  g_bus_get (G_BUS_TYPE_SYSTEM,
             cancellable,
             sysprof_check_supported_bus_cb,
             g_steal_pointer (&task));

}

gboolean
sysprof_check_supported_finish (GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
