/* sysprof-power-profiles.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <gtk/gtk.h>

#include "sysprof-power-profiles.h"

static const char *fallback[] = { "", "power-saver", "performance", "balanced", NULL };

static void
free_string (gpointer strptr)
{
  char **str = strptr;
  g_clear_pointer (str, g_free);
}

GListModel *
sysprof_power_profiles_new (void)
{
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GVariant) child = NULL;
  g_autoptr(GVariant) grandchild = NULL;
  g_autoptr(GVariant) vdict = NULL;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GArray) strings = NULL;
  char *empty;
  GVariantIter iter;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  if (bus == NULL)
    goto failure;

  result = g_dbus_connection_call_sync (bus,
                                        "org.freedesktop.UPower.PowerProfiles",
                                        "/org/freedesktop/UPower/PowerProfiles",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        g_variant_new ("(ss)",
                                                       "org.freedesktop.UPower.PowerProfiles",
                                                       "Profiles"),
                                        G_VARIANT_TYPE ("(v)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        NULL);
  if (result == NULL)
    goto failure;

  strings = g_array_new (TRUE, FALSE, sizeof (char*));
  empty = g_strdup ("");
  g_array_append_val (strings, empty);

  g_array_set_clear_func (strings, free_string);
  child = g_variant_get_child_value (result, 0);
  grandchild = g_variant_get_child_value (child, 0);
  g_variant_iter_init (&iter, grandchild);
  while (g_variant_iter_loop (&iter, "@a{sv}", &vdict))
    {
      char *name = NULL;

      if (g_variant_lookup (vdict, "Profile", "s", &name))
        g_array_append_val (strings, name);
    }

  return G_LIST_MODEL (gtk_string_list_new ((const char * const *)(gpointer)strings->data));

failure:
  return G_LIST_MODEL (gtk_string_list_new (fallback));
}
