/* sysprofd.c
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

#define G_LOG_DOMAIN "sysprofd"

#include "config.h"

#include <gio/gio.h>
#include <stdlib.h>

#include "ipc-service.h"
#include "ipc-service-impl.h"

#define BUS_NAME                "org.gnome.Sysprof3"
#define OBJECT_PATH             "/org/gnome/Sysprof3"
#define NAME_ACQUIRE_DELAY_SECS 3

static GMainLoop *main_loop;
static gboolean   name_acquired;
static gint       exit_status = EXIT_SUCCESS;

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_message ("Acquired Bus Name: %s", name);
  name_acquired = TRUE;
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  /* Exit if we lost the name */
  if (g_strcmp0 (name, BUS_NAME) == 0)
    {
      g_message ("Lost Bus Name: %s, exiting.", name);
      name_acquired = FALSE;
      g_main_loop_quit (main_loop);
    }
}

static gboolean
wait_for_acquire_timeout_cb (gpointer data)
{
  if (!name_acquired)
    {
      exit_status = EXIT_FAILURE;
      g_critical ("Failed to acquire name on bus after %d seconds, exiting.",
                  NAME_ACQUIRE_DELAY_SECS);
      g_main_loop_quit (main_loop);
    }

  return G_SOURCE_REMOVE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GError) error = NULL;
  GBusType bus_type = G_BUS_TYPE_SYSTEM;

  g_set_prgname ("sysprofd");
  g_set_application_name ("sysprofd");

  if (g_getenv ("SYSPROFD_USE_SESSION_BUS"))
    bus_type = G_BUS_TYPE_SESSION;

  main_loop = g_main_loop_new (NULL, FALSE);

  if ((bus = g_bus_get_sync (bus_type, NULL, &error)))
    {
      g_autoptr(IpcService) service = ipc_service_impl_new ();

      if (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (service),
                                            bus,
                                            OBJECT_PATH,
                                            &error))
        {
          g_bus_own_name_on_connection (bus,
                                        BUS_NAME,
                                        (G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                         G_BUS_NAME_OWNER_FLAGS_REPLACE),
                                        name_acquired_cb,
                                        name_lost_cb,
                                        NULL,
                                        NULL);
          g_timeout_add_seconds (NAME_ACQUIRE_DELAY_SECS,
                                 wait_for_acquire_timeout_cb,
                                 NULL);
          g_main_loop_run (main_loop);
          g_main_loop_unref (main_loop);

          return exit_status;
        }
    }

  g_error ("Failed to setup system bus: %s", error->message);

  g_main_loop_unref (main_loop);

  return EXIT_FAILURE;
}
