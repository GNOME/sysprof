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
#include <sysprof-capture.h>

#include "ipc-service.h"

#include "ipc-rapl-profiler.h"
#include "ipc-service-impl.h"
#include "ipc-unwinder-impl.h"

#define V3_PATH                 "/org/gnome/Sysprof3"
#define RAPL_PATH               "/org/gnome/Sysprof3/RAPL"
#define UNWINDER_PATH           "/org/gnome/Sysprof3/Unwinder"
#define NAME_ACQUIRE_DELAY_SECS 3
#define INACTIVITY_TIMEOUT_SECS 120

static const gchar *bus_names[] = { "org.gnome.Sysprof3" };
static GMainLoop   *main_loop;
static gboolean     name_acquired;
static gint         exit_status = EXIT_SUCCESS;

static guint inactivity;
static G_LOCK_DEFINE (activity);

static gboolean
inactivity_cb (gpointer data)
{
  inactivity = 0;
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

static void
activity_cb (GObject  *object,
             gpointer  user_data)
{
  G_LOCK (activity);
  if (inactivity)
    g_source_remove (inactivity);
  inactivity = g_timeout_add_seconds (INACTIVITY_TIMEOUT_SECS,
                                      inactivity_cb,
                                      NULL);
  G_UNLOCK (activity);
}

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
  /* Exit if we lost one of our bus names */
  for (guint i = 0; i < G_N_ELEMENTS (bus_names); i++)
    {
      if (g_strcmp0 (name, bus_names[i]) == 0)
        {
          g_message ("Lost Bus Name: %s, exiting.", bus_names[i]);
          name_acquired = FALSE;
          g_main_loop_quit (main_loop);
        }
    }
}

static gboolean
wait_for_acquire_timeout_cb (gpointer data)
{
  const gchar *bus_name = data;

  if (!name_acquired)
    {
      exit_status = EXIT_FAILURE;
      g_critical ("Failed to acquire %s on bus after %d seconds, exiting.",
                  bus_name, NAME_ACQUIRE_DELAY_SECS);
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

  sysprof_clock_init ();

  g_set_prgname ("sysprofd");
  g_set_application_name ("sysprofd");

  main_loop = g_main_loop_new (NULL, FALSE);

  if ((bus = g_bus_get_sync (bus_type, NULL, &error)))
    {
      g_autoptr(IpcProfiler) rapl = ipc_rapl_profiler_new ();
      g_autoptr(IpcService) v3_service = ipc_service_impl_new ();
      g_autoptr(IpcUnwinder) unwinder = ipc_unwinder_impl_new ();

      g_signal_connect (v3_service, "activity", G_CALLBACK (activity_cb), NULL);
      g_signal_connect (rapl, "activity", G_CALLBACK (activity_cb), NULL);

      activity_cb (NULL, NULL);

      if (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (v3_service), bus, V3_PATH, &error) &&
          g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (rapl), bus, RAPL_PATH, &error) &&
          g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (unwinder), bus, UNWINDER_PATH, &error))
        {
          for (guint i = 0; i < G_N_ELEMENTS (bus_names); i++)
            {
              g_bus_own_name_on_connection (bus,
                                            bus_names[i],
                                            (G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                             G_BUS_NAME_OWNER_FLAGS_REPLACE),
                                            name_acquired_cb,
                                            name_lost_cb,
                                            NULL,
                                            NULL);
              g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                          NAME_ACQUIRE_DELAY_SECS,
                                          wait_for_acquire_timeout_cb,
                                          g_strdup (bus_names[i]),
                                          g_free);
            }

          g_main_loop_run (main_loop);
          g_main_loop_unref (main_loop);

          return exit_status;
        }
    }

  g_error ("Failed to setup system bus: %s", error->message);

  g_main_loop_unref (main_loop);

  return EXIT_FAILURE;
}
