/*
 * test-recording-connection.c
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
#include <libdex.h>
#include <unistd.h>

#include "ipc-peer-private.h"
#include "ipc-service-impl.h"
#include "sysprof-recording-private.h"
#include "sysprof-service-private.h"
#include "sysprof-util-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static GTestDBus *test_bus;
static GDBusConnection *server;
static guint checks;
static gboolean authorized = TRUE;
static gboolean fail_prepare;
static gboolean expect_private = TRUE;
static gboolean expect_connection = TRUE;
static gboolean malformed_reply;
static char *last_subject;

static void record_once (gboolean use_sysprofd,
                         gboolean private,
                         gboolean fail);

static const char authority_xml[] =
  "<node><interface name='org.freedesktop.PolicyKit1.Authority'>"
  "<method name='CheckAuthorization'>"
  "<arg type='(sa{sv})' direction='in'/>"
  "<arg type='s' direction='in'/>"
  "<arg type='a{ss}' direction='in'/>"
  "<arg type='u' direction='in'/>"
  "<arg type='s' direction='in'/>"
  "<arg type='(bba{ss})' direction='out'/>"
  "</method></interface></node>";

static void
check_authorization (GDBusConnection       *connection,
                     const char            *sender,
                     const char            *object_path,
                     const char            *interface_name,
                     const char            *method_name,
                     GVariant              *parameters,
                     GDBusMethodInvocation *invocation,
                     gpointer               user_data)
{
  g_autoptr(GVariant) subject = NULL;
  g_autoptr(GVariant) details = NULL;
  g_autoptr(GVariant) action = NULL;
  const char *kind;
  const char *name;

  g_assert_cmpstr (method_name, ==, "CheckAuthorization");
  subject = g_variant_get_child_value (parameters, 0);
  action = g_variant_get_child_value (parameters, 1);
  g_variant_get (subject, "(&s@a{sv})", &kind, &details);
  g_assert_cmpstr (kind, ==, "system-bus-name");
  g_assert_true (g_variant_lookup (details, "name", "&s", &name));
  g_assert_cmpstr (g_variant_get_string (action, NULL), ==, "org.gnome.sysprof3.profile");

  g_free (last_subject);
  last_subject = g_strdup (name);
  checks++;
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("((bb@a{ss}))",
                                                        authorized,
                                                        FALSE,
                                                        g_variant_new_array (G_VARIANT_TYPE ("{ss}"),
                                                                             NULL,
                                                                             0)));
}

static GDBusConnection *
connect_to_bus (void)
{
  GDBusConnection *connection;
  g_autoptr(GError) error = NULL;

  connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (test_bus),
                                                       (G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                         G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                                       NULL,
                                                       NULL,
                                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);
  return connection;
}

static GVariant *
call (GDBusConnection  *connection,
      const char       *path,
      const char       *interface,
      const char       *method,
      GVariant         *parameters,
      GError          **error)
{
  return dex_await_variant (dex_dbus_connection_call (connection,
                                                      _sysprof_service_get_name (connection),
                                                      path, interface, method, parameters,
                                                      NULL, G_DBUS_CALL_FLAGS_NONE, 5000), error);
}

static void
wait_for_peers (gboolean active)
{
  for (guint i = 0; i < 500 && _ipc_peer_has_active () != active; i++)
    dex_await (dex_timeout_new_msec (10), NULL);
  g_assert_cmpint (_ipc_peer_has_active (), ==, active);
}

static void
test_private_connection (void)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GDBusConnection) peer = NULL;
  g_autoptr(GDBusConnection) second = NULL;
  g_autoptr(GDBusConnection) denied = NULL;
  g_autoptr(IpcService) service = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;

  checks = 0;
  authorized = TRUE;
  bus = connect_to_bus ();
  service = ipc_service_impl_new (FALSE);
  g_assert_true (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (service),
                                                   server, "/org/gnome/Sysprof3", &error));
  g_assert_no_error (error);

  peer = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_no_error (error);
  g_assert_nonnull (peer);
  g_assert_null (_sysprof_service_get_name (peer));
  g_assert_cmpuint (checks, ==, 1);
  g_assert_cmpstr (last_subject, ==, g_dbus_connection_get_unique_name (bus));
  g_assert_true (g_dbus_connection_get_capabilities (peer) & G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING);

  for (guint i = 0; i < 8; i++)
    {
      g_autoptr(GBytes) bytes = dex_await_boxed (sysprof_get_proc_file_bytes (peer, "/proc/self/status"), &error);

      g_assert_no_error (error);
      g_assert_nonnull (bytes);
      g_assert_cmpuint (g_bytes_get_size (bytes), >, 0);
    }

  /* Exercise each interface, including the FD-bearing unwinder signature.
   * Invalid stack size avoids needing root, perf, or an installed unwinder.
   */
  reply = call (peer,
                "/org/gnome/Sysprof3/RAPL",
                "org.gnome.Sysprof3.Profiler",
                "Stop",
                NULL,
                &error);
  g_assert_no_error (error);
  g_clear_pointer (&reply, g_variant_unref);

  reply = call (peer,
                "/org/gnome/Sysprof3/Unwinder",
                "org.gnome.Sysprof3.Unwinder",
                "Unwind",
                g_variant_new ("(u@a(hi)hh)",
                               0,
                               g_variant_new_array (G_VARIANT_TYPE ("(hi)"), NULL, 0),
                               -1,
                               -1),
                &error);
  g_assert_null (reply);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_clear_error (&error);
  g_assert_cmpuint (checks, ==, 1);

  /* A received FD remains usable when it is transferred over the peer. */
  {
    g_autoptr(DexFuture) future = NULL;
    g_autoptr(GUnixFDList) fd_list = NULL;
    g_autofd int fd = -1;
    char buffer[64];
    int handle;

    future = dex_dbus_connection_call_with_unix_fd_list (peer,
                                                         NULL,
                                                         "/org/gnome/Sysprof3",
                                                         "org.gnome.Sysprof3.Service",
                                                         "GetProcFd",
                                                         g_variant_new ("(^ay)", "/proc/self/status"),
                                                         G_VARIANT_TYPE ("(h)"),
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         5000,
                                                         NULL);
    g_assert_true (dex_await (dex_ref (future), &error));
    g_assert_no_error (error);
    reply = dex_await_variant (dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0)), &error);
    g_assert_no_error (error);
    fd_list = dex_await_object (dex_ref (dex_future_set_get_future_at (DEX_FUTURE_SET (future), 1)),
                                &error);
    g_assert_no_error (error);
    g_variant_get (reply, "(h)", &handle);
    fd = g_unix_fd_list_get (fd_list, handle, &error);
    g_assert_no_error (error);
    g_assert_cmpint (read (fd, buffer, sizeof buffer), >, 0);
    g_clear_pointer (&reply, g_variant_unref);
  }

  reply = call (peer,
                "/org/gnome/Sysprof3",
                "org.gnome.Sysprof3.Service",
                "OpenRecordingConnection",
                NULL,
                &error);
  g_assert_null (reply);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
  g_clear_error (&error);
  g_assert_cmpuint (checks, ==, 1);

  second = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_no_error (error);
  g_assert_nonnull (second);
  g_assert_cmpuint (checks, ==, 2);

  /* Denial must not fall back, and legacy bus calls must still authorize. */
  authorized = FALSE;
  denied = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_null (denied);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
  g_clear_error (&error);
  g_assert_cmpuint (checks, ==, 3);

  reply = call (bus,
                "/org/gnome/Sysprof3",
                "org.gnome.Sysprof3.Service",
                "GetProcFile",
                g_variant_new ("(^ay)", "/proc/self/status"),
                &error);
  g_assert_null (reply);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
  g_clear_error (&error);
  g_assert_cmpuint (checks, ==, 4);

  /* Existing grants survive a subsequent policy denial. */
  reply = call (second,
                "/org/gnome/Sysprof3",
                "org.gnome.Sysprof3.Service",
                "GetProcFile",
                g_variant_new ("(^ay)", "/proc/self/status"),
                &error);
  g_assert_no_error (error);
  g_clear_pointer (&reply, g_variant_unref);
  g_assert_cmpuint (checks, ==, 4);

  g_assert_true (g_dbus_connection_close_sync (peer, NULL, &error));
  g_assert_no_error (error);
  wait_for_peers (TRUE);
  reply = call (second,
                "/org/gnome/Sysprof3",
                "org.gnome.Sysprof3.Service",
                "ListProcesses",
                NULL,
                &error);
  g_assert_no_error (error);
  g_clear_pointer (&reply, g_variant_unref);
  g_assert_true (g_dbus_connection_close_sync (second, NULL, &error));
  g_assert_no_error (error);
  wait_for_peers (FALSE);

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (service));
  g_assert_true (g_dbus_connection_close_sync (bus, NULL, &error));
  g_assert_no_error (error);
}

static void
test_abandoned_connection (void)
{
  g_autoptr(GError) error = NULL;
  g_autofd int fd = _ipc_peer_open (&error);

  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_true (_ipc_peer_has_active ());
  g_clear_fd (&fd, NULL);
  wait_for_peers (FALSE);
}

static void
legacy_call (GDBusConnection       *connection,
             const char            *sender,
             const char            *object_path,
             const char            *interface_name,
             const char            *method_name,
             GVariant              *parameters,
             GDBusMethodInvocation *invocation,
             gpointer               user_data)
{
  if (g_str_equal (method_name, "GetProcFile"))
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(^ay)", "legacy"));
  else if (malformed_reply)
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(h)", 0));
  else
    g_dbus_method_invocation_return_error_literal (invocation,
                                                   G_DBUS_ERROR,
                                                   G_DBUS_ERROR_FAILED,
                                                   "Connection setup failed");
}

static void
test_fallback (void)
{
  static const GDBusInterfaceVTable vtable = { .method_call = legacy_call };
  const char *xml =
    "<node><interface name='org.gnome.Sysprof3.Service'>"
    "<method name='GetProcFile'><arg type='ay' direction='in'/>"
    "<arg type='ay' direction='out'/></method>"
    "</interface></node>";
  const char *new_xml =
    "<node><interface name='org.gnome.Sysprof3.Service'>"
    "<method name='OpenRecordingConnection'><arg type='h' direction='out'/></method>"
    "</interface></node>";
  g_autoptr(GDBusNodeInfo) info = NULL;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;
  guint registration;

  bus = connect_to_bus ();
  info = g_dbus_node_info_new_for_xml (xml, &error);
  g_assert_no_error (error);
  registration = g_dbus_connection_register_object (server, "/org/gnome/Sysprof3",
                                                      info->interfaces[0], &vtable, NULL, NULL, &error);
  g_assert_no_error (error);
  connection = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_no_error (error);
  g_assert_true (connection == bus);
  g_assert_cmpstr (_sysprof_service_get_name (connection), ==, "org.gnome.Sysprof3");
  bytes = dex_await_boxed (sysprof_get_proc_file_bytes (connection, "/proc/self/status"), &error);
  g_assert_no_error (error);
  g_assert_cmpmem (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), "legacy", 6);
  g_clear_object (&connection);
  authorized = TRUE;
  checks = 0;
  record_once (TRUE, FALSE, FALSE);
  g_assert_cmpuint (checks, ==, 1);
  g_dbus_connection_unregister_object (server, registration);
  g_clear_pointer (&info, g_dbus_node_info_unref);

  info = g_dbus_node_info_new_for_xml (new_xml, &error);
  g_assert_no_error (error);
  registration = g_dbus_connection_register_object (server, "/org/gnome/Sysprof3",
                                                      info->interfaces[0], &vtable, NULL, NULL, &error);
  g_assert_no_error (error);
  malformed_reply = FALSE;
  connection = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_null (connection);
  g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED);
  g_clear_error (&error);
  malformed_reply = TRUE;
  connection = dex_await_object (_sysprof_service_connect (bus), &error);
  g_assert_null (connection);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);
  g_dbus_connection_unregister_object (server, registration);
  g_assert_true (g_dbus_connection_close_sync (bus, NULL, &error));
  g_assert_no_error (error);
}

/* A minimal instrument exercises the recording's policy acquisition and
 * capture-file helpers, including cleanup when prepare fails.
 */
typedef struct { SysprofInstrument parent; } TestInstrument;
typedef struct { SysprofInstrumentClass parent; } TestInstrumentClass;
G_DEFINE_TYPE (TestInstrument, test_instrument, SYSPROF_TYPE_INSTRUMENT)

static char **
test_instrument_policy (SysprofInstrument *instrument)
{
  const char *policies[] = { "org.gnome.sysprof3.profile", NULL };

  return g_strdupv ((char **)policies);
}

static DexFuture *
test_instrument_prepare (SysprofInstrument *instrument,
                         SysprofRecording  *recording)
{
  g_autoptr(GDBusConnection) connection = _sysprof_recording_dup_connection (recording);

  if (expect_connection)
    {
      g_assert_nonnull (connection);
      g_assert_cmpint (_sysprof_service_get_name (connection) == NULL, ==, expect_private);
    }
  else
    {
      g_assert_null (connection);
    }

  if (fail_prepare)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_FAILED,
                                  "Prepare failed");

  return _sysprof_recording_add_file (recording, "/proc/self/status", FALSE);
}

static void
test_instrument_class_init (TestInstrumentClass *klass)
{
  SysprofInstrumentClass *instrument_class = (SysprofInstrumentClass *)klass;

  instrument_class->list_required_policy = test_instrument_policy;
  instrument_class->prepare = test_instrument_prepare;
}

static void
test_instrument_init (TestInstrument *self)
{
}

static void
record_once (gboolean use_sysprofd,
             gboolean private,
             gboolean fail)
{
  g_autoptr(SysprofInstrument) instrument = g_object_new (test_instrument_get_type (), NULL);
  g_autoptr(SysprofRecording) recording = NULL;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = sysprof_memfd_create ("test-recording-connection");

  g_assert_cmpint (fd, >=, 0);
  writer = sysprof_capture_writer_new_from_fd (g_steal_fd (&fd), 0);
  recording = _sysprof_recording_new (writer, NULL, &instrument, 1, use_sysprofd);
  expect_private = private;
  expect_connection = use_sysprofd;
  fail_prepare = fail;
  _sysprof_recording_start (recording);

  if (fail_prepare)
    {
      g_assert_false (dex_await (dex_ref (recording->fiber), &error));
      g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
      g_clear_error (&error);
    }
  else
    {
      g_assert_true (dex_await (dex_ref (recording->fiber), &error));
      g_assert_no_error (error);
    }
  g_assert_null (recording->connection);
  wait_for_peers (FALSE);
}

static void
test_recording_without_daemon (void)
{
  checks = 0;
  authorized = FALSE;
  record_once (FALSE, FALSE, FALSE);
  g_assert_cmpuint (checks, ==, 0);
}

static void
test_recording_lifetime (void)
{
  g_autoptr(IpcService) service = NULL;
  g_autoptr(GError) error = NULL;

  checks = 0;
  authorized = TRUE;
  service = ipc_service_impl_new (FALSE);
  g_assert_true (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (service),
                                                   server, "/org/gnome/Sysprof3", &error));
  g_assert_no_error (error);

  for (guint i = 0; i < 2; i++)
    {
      record_once (TRUE, TRUE, i > 0);
      g_assert_cmpuint (checks, ==, i + 1);
    }

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (service));
}

int
main (int   argc,
      char *argv[])
{
  static const GDBusInterfaceVTable vtable = { .method_call = check_authorization };
  const char *names[] = { "org.gnome.Sysprof3", "org.freedesktop.PolicyKit1" };
  g_autoptr(GDBusNodeInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  guint registration;
  int ret;

  g_test_init (&argc, &argv, NULL);
  sysprof_clock_init ();
  test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (test_bus);
  g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address (test_bus), TRUE);
  dex_init ();
  server = connect_to_bus ();

  for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
      g_autoptr(GVariant) reply = NULL;

      reply = g_dbus_connection_call_sync (server, "org.freedesktop.DBus",
                                            "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                            "RequestName", g_variant_new ("(su)", names[i], 0),
                                            G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE,
                                            5000, NULL, &error);
      g_assert_no_error (error);
      g_assert_nonnull (reply);
    }

  info = g_dbus_node_info_new_for_xml (authority_xml, &error);
  g_assert_no_error (error);
  registration = g_dbus_connection_register_object (server, "/org/freedesktop/PolicyKit1/Authority",
                                                      info->interfaces[0], &vtable, NULL, NULL, &error);
  g_assert_no_error (error);

  dex_test_add_func ("/recording-connection/private", test_private_connection);
  dex_test_add_func ("/recording-connection/fallback", test_fallback);
  dex_test_add_func ("/recording-connection/abandoned", test_abandoned_connection);
  dex_test_add_func ("/recording-connection/lifetime", test_recording_lifetime);
  dex_test_add_func ("/recording-connection/without-daemon", test_recording_without_daemon);
  ret = g_test_run ();

  g_dbus_connection_unregister_object (server, registration);
  g_dbus_connection_close_sync (server, NULL, NULL);
  g_clear_object (&server);
  g_clear_pointer (&last_subject, g_free);
  g_test_dbus_down (test_bus);
  g_clear_object (&test_bus);

  return ret;
}
