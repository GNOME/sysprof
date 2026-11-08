/*
 * ipc-peer.c
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

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "ipc-peer-private.h"
#include "ipc-rapl-profiler.h"
#include "ipc-service-impl.h"
#include "ipc-unwinder-impl.h"

typedef struct
{
  GSocketConnection *stream;
  GDBusConnection   *connection;
  GCancellable      *cancellable;
  IpcService        *service;
  IpcProfiler       *rapl;
  IpcUnwinder       *unwinder;
  gulong             closed_handler;
  guint              timeout;
} IpcPeer;

static int n_peers;

static void
ipc_peer_free (IpcPeer *peer)
{
  g_assert (peer != NULL);

  g_clear_handle_id (&peer->timeout, g_source_remove);

  if (peer->connection != NULL)
    {
      g_clear_signal_handler (&peer->closed_handler, peer->connection);
      g_dbus_connection_close (peer->connection, NULL, NULL, NULL);
    }

  if (peer->service != NULL &&
      g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (peer->service)) != NULL)
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (peer->service));

  if (peer->rapl != NULL)
    {
      if (g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (peer->rapl)) != NULL)
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (peer->rapl));
      ipc_rapl_profiler_shutdown (IPC_RAPL_PROFILER (peer->rapl));
    }

  if (peer->unwinder != NULL)
    {
      if (g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (peer->unwinder)) != NULL)
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (peer->unwinder));
      ipc_unwinder_impl_shutdown (IPC_UNWINDER_IMPL (peer->unwinder));
    }

  g_clear_object (&peer->service);
  g_clear_object (&peer->rapl);
  g_clear_object (&peer->unwinder);
  g_clear_object (&peer->connection);
  g_clear_object (&peer->stream);
  g_clear_object (&peer->cancellable);
  g_atomic_int_add (&n_peers, -1);
  g_free (peer);
}

static void
ipc_peer_closed_cb (GDBusConnection *connection,
                    gboolean         remote_peer_vanished,
                    const GError    *error,
                    gpointer         user_data)
{
  IpcPeer *peer = user_data;

  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_assert (peer != NULL);

  ipc_peer_free (peer);
}

static gboolean
ipc_peer_timeout_cb (gpointer user_data)
{
  IpcPeer *peer = user_data;

  peer->timeout = 0;
  g_cancellable_cancel (peer->cancellable);

  return G_SOURCE_REMOVE;
}

static gboolean
ipc_peer_allow_mechanism_cb (GDBusAuthObserver *observer,
                             const char        *mechanism,
                             gpointer           user_data)
{
  /* The FD was delivered to an authorized system-bus caller. Possession of
   * that FD is the grant; socketpair credentials identify its creator and
   * cannot identify the recipient, which may also be in a user namespace.
   * Still perform the D-Bus handshake to negotiate Unix FD passing.
   */
  return g_str_equal (mechanism, "ANONYMOUS");
}

static void
ipc_peer_connection_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  IpcPeer *peer = user_data;
  g_autoptr(GError) error = NULL;

  g_clear_handle_id (&peer->timeout, g_source_remove);

  if (!(peer->connection = g_dbus_connection_new_finish (result, &error)))
    {
      ipc_peer_free (peer);
      return;
    }

  g_dbus_connection_set_exit_on_close (peer->connection, FALSE);

  peer->service = ipc_service_impl_new (TRUE);
  peer->rapl = ipc_rapl_profiler_new (TRUE);
  peer->unwinder = ipc_unwinder_impl_new (TRUE);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (peer->service),
                                         peer->connection,
                                         "/org/gnome/Sysprof3",
                                         &error) ||
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (peer->rapl),
                                         peer->connection,
                                         "/org/gnome/Sysprof3/RAPL",
                                         &error) ||
      !g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (peer->unwinder),
                                         peer->connection,
                                         "/org/gnome/Sysprof3/Unwinder",
                                         &error))
    {
      ipc_peer_free (peer);
      return;
    }

  peer->closed_handler = g_signal_connect (peer->connection, "closed",
                                           G_CALLBACK (ipc_peer_closed_cb), peer);

  if (g_dbus_connection_is_closed (peer->connection))
    ipc_peer_free (peer);
  else
    g_dbus_connection_start_message_processing (peer->connection);
}

static gboolean
ipc_peer_start_cb (gpointer user_data)
{
  IpcPeer *peer = user_data;
  g_autoptr(GDBusAuthObserver) observer = NULL;
  g_autofree char *guid = NULL;

  observer = g_dbus_auth_observer_new ();
  g_signal_connect (observer, "allow-mechanism",
                    G_CALLBACK (ipc_peer_allow_mechanism_cb), NULL);
  guid = g_dbus_generate_guid ();
  peer->cancellable = g_cancellable_new ();
  peer->timeout = g_timeout_add_seconds (30, ipc_peer_timeout_cb, peer);

  g_dbus_connection_new (G_IO_STREAM (peer->stream),
                         guid,
                         (G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER |
                           G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS |
                           G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING),
                         observer,
                         peer->cancellable,
                         ipc_peer_connection_cb,
                         peer);

  return G_SOURCE_REMOVE;
}

/* Only call after authorizing the system-bus invocation. The returned FD is
 * a capability for the profiling APIs, valid until the connection is closed.
 */
int
_ipc_peer_open (GError **error)
{
  g_autoptr(GSocket) socket = NULL;
  g_autofd int server_fd = -1;
  g_autofd int client_fd = -1;
  IpcPeer *peer;
  int fds[2];

  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
    {
      int errsv = errno;

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errsv),
                   "Failed to create recording connection: %s",
                   g_strerror (errsv));
      return -1;
    }

  server_fd = fds[0];
  client_fd = fds[1];

  if (!(socket = g_socket_new_from_fd (server_fd, error)))
    return -1;
  server_fd = -1;

  peer = g_new0 (IpcPeer, 1);
  peer->stream = g_socket_connection_factory_create_connection (socket);
  g_atomic_int_inc (&n_peers);

  /* Method handlers run in worker threads. Export the peer interfaces on
   * the daemon's main context, and do not wait for the handshake before
   * returning the client's endpoint.
   */
  g_idle_add_full (G_PRIORITY_DEFAULT, ipc_peer_start_cb, peer, NULL);

  return g_steal_fd (&client_fd);
}

gboolean
_ipc_peer_has_active (void)
{
  return g_atomic_int_get (&n_peers) > 0;
}
