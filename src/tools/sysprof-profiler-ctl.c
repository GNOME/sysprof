/* sysprof-profiler-ctl.c
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

#define G_LOG_DOMAIN "sysprof-profiler-ctl"

#include "config.h"

#include <fcntl.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <unistd.h>

#include "ipc-profiler.h"

static gboolean opt_force;
static gboolean opt_system;
static gboolean opt_session;
static gchar *opt_address;
static gchar *opt_dest;
static gchar *opt_object_path;
static gint opt_timeout = -1;

static const GOptionEntry main_entries[] = {
  { "system", 'y', 0, G_OPTION_ARG_NONE, &opt_system, N_("Connect to the system bus") },
  { "session", 'e', 0, G_OPTION_ARG_NONE, &opt_session, N_("Connect to the session bus") },
  { "address", 'a', 0, G_OPTION_ARG_STRING, &opt_address, N_("Connect to the given D-Bus address") },
  { "dest", 'd', 0, G_OPTION_ARG_STRING, &opt_dest, N_("Destination D-Bus name to invoke method on") },
  { "object-path", 'o', 0, G_OPTION_ARG_STRING, &opt_object_path, N_("Object path to invoke method on"), N_("/org/gnome/Sysprof3/Profiler") },
  { "timeout", 't', 0, G_OPTION_ARG_INT, &opt_timeout, N_("Timeout in seconds") },
  { "force", 'f', 0, G_OPTION_ARG_NONE, &opt_force, N_("Overwrite FILENAME if it exists") },
  { 0 }
};

static gboolean
handle_sigint (gpointer data)
{
  GMainLoop *main_loop = data;
  g_printerr ("\nSIGINT received, stopping profiler.\n");
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autofree gchar *filename = NULL;
  guint flags = O_RDWR;
  gint fd;
  gint handle;

  context = g_option_context_new (_("--dest=BUS_NAME [FILENAME] - connect to an embedded sysprof profiler"));
  g_option_context_add_main_entries (context, main_entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (opt_dest == NULL)
    {
      g_printerr ("--dest= must contain a peer bus name on the D-Bus.\n");
      return EXIT_FAILURE;
    }

  if ((opt_system && opt_session) ||
      (opt_address && (opt_system || opt_session)))
    {
      g_printerr ("Only one of --system --session or --address may be specified.");
      return EXIT_FAILURE;
    }

  if (!opt_address && !opt_session && !opt_system)
    opt_session = TRUE;

  main_loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGINT, handle_sigint, main_loop);

  if (opt_session)
    bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  else if (opt_system)
    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  else
    bus = g_dbus_connection_new_for_address_sync (opt_address,
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                  NULL,
                                                  NULL,
                                                  &error);

  if (bus == NULL)
    {
      g_printerr ("Failed to connect to D-Bus: %s\n", error->message);
      return EXIT_FAILURE;
    }

  filename = (argc == 1) ? g_strdup_printf ("sysprof.%d.syscap", getpid ())
                         : g_strdup (argv[1]);

  if (!opt_force)
    flags |= O_CREAT;

  if (-1 == (fd = g_open (filename, flags, 0644)))
    {
      g_printerr ("Failed to open file \"%s\". Try --force.\n", filename);
      return EXIT_FAILURE;
    }

  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, fd, &error);

  close (fd);
  fd = -1;

  if (handle == -1)
    {
      g_printerr ("Failed to build FD list for peer.\n");
      g_unlink (filename);
      return EXIT_FAILURE;
    }

  reply = g_dbus_connection_call_with_unix_fd_list_sync (bus,
                                                         opt_dest,
                                                         opt_object_path,
                                                         "org.gnome.Sysprof3.Profiler",
                                                         "Start",
                                                         g_variant_new ("(a{sv}h)", NULL, handle),
                                                         G_VARIANT_TYPE ("()"),
                                                         G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                         opt_timeout,
                                                         fd_list,
                                                         NULL,
                                                         NULL,
                                                         &error);

  if (error != NULL)
    {
      g_printerr ("Failed to start profiler: %s\n", error->message);
      g_unlink (filename);
      return EXIT_FAILURE;
    }

  g_printerr ("Profiler capturing to file \"%s\"\n", filename);

  g_clear_pointer (&reply, g_variant_unref);

  g_main_loop_run (main_loop);

  reply = g_dbus_connection_call_sync (bus,
                                       opt_dest,
                                       opt_object_path,
                                       "org.gnome.Sysprof3.Profiler",
                                       "Stop",
                                       g_variant_new ("()"),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       opt_timeout,
                                       NULL,
                                       &error);

  if (error != NULL)
    {
      g_printerr ("Failed to stop profiler: %s\n", error->message);
      return EXIT_FAILURE;
    }

  g_printerr ("Profiler stopped.\n");

  return EXIT_SUCCESS;
}
