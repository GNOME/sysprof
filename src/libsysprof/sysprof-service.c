/*
 * sysprof-service.c
 *
 * Copyright 2026 Christian Hergert
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

#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include "sysprof-service-private.h"

const char *
_sysprof_service_get_name (GDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

  return g_dbus_connection_get_unique_name (connection) ? "org.gnome.Sysprof3" : NULL;
}

static void
sysprof_service_open_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GSocket) socket = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = -1;
  int handle;

  if (!(reply = g_dbus_connection_call_with_unix_fd_list_finish (G_DBUS_CONNECTION (object),
                                                                 &fd_list,
                                                                 result,
                                                                 &error)))
    goto failure;

  if (fd_list == NULL)
    {
      g_set_error_literal (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Missing recording connection FD");
      goto failure;
    }

  g_variant_get (reply, "(h)", &handle);

  if ((fd = g_unix_fd_list_get (fd_list, handle, &error)) == -1 ||
      !(socket = g_socket_new_from_fd (fd, &error)))
    goto failure;
  fd = -1;

  dex_promise_resolve_object (promise, g_steal_pointer (&socket));
  return;

failure:
  dex_promise_reject (promise, g_steal_pointer (&error));
}

static void
sysprof_service_connection_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;

  if (!(connection = g_dbus_connection_new_finish (result, &error)))
    {
      dex_promise_reject (promise, g_steal_pointer (&error));
    }
  else
    {
      g_dbus_connection_set_exit_on_close (connection, FALSE);
      dex_promise_resolve_object (promise, g_steal_pointer (&connection));
    }
}

static DexFuture *
sysprof_service_connect_fiber (gpointer user_data)
{
  GDBusConnection *bus = user_data;
  g_autoptr(DexPromise) promise = NULL;
  g_autoptr(GSocket) socket = NULL;
  g_autoptr(GSocketConnection) stream = NULL;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (bus));

  promise = dex_promise_new_cancellable ();
  g_dbus_connection_call_with_unix_fd_list (bus,
                                            "org.gnome.Sysprof3",
                                            "/org/gnome/Sysprof3",
                                            "org.gnome.Sysprof3.Service",
                                            "OpenRecordingConnection",
                                            NULL,
                                            G_VARIANT_TYPE ("(h)"),
                                            G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                            G_MAXINT,
                                            NULL,
                                            dex_promise_get_cancellable (promise),
                                            sysprof_service_open_cb,
                                            dex_ref (promise));

  if (!(socket = dex_await_object (dex_ref (promise), &error)))
    {
      /* Only old daemons use the bus path. A denied grant or a broken peer
       * connection must not silently change the authorization mechanism.
       */
      if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
        return dex_future_new_for_object (bus);

      return dex_future_new_for_error (g_steal_pointer (&error));
    }

  stream = g_socket_connection_factory_create_connection (socket);
  dex_clear (&promise);
  promise = dex_promise_new_cancellable ();

  g_dbus_connection_new (G_IO_STREAM (stream),
                          NULL,
                          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                          NULL,
                          dex_promise_get_cancellable (promise),
                          sysprof_service_connection_cb,
                          dex_ref (promise));

  if (!(connection = dex_await_object (dex_future_first (dex_ref (promise),
                                                         dex_timeout_new_seconds (30),
                                                         NULL),
                                       &error)))
    {
      g_cancellable_cancel (dex_promise_get_cancellable (promise));
      return dex_future_new_for_error (g_steal_pointer (&error));
    }

  return dex_future_new_for_object (connection);
}

DexFuture *
_sysprof_service_connect (GDBusConnection *bus)
{
  dex_return_error_if_fail (G_IS_DBUS_CONNECTION (bus));

  return dex_scheduler_spawn (NULL,
                              0,
                              sysprof_service_connect_fiber,
                              g_object_ref (bus),
                              g_object_unref);
}
