/*
 * sysprof-util.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-util-private.h"

DexFuture *
sysprof_get_proc_file_bytes (GDBusConnection *connection,
                             const char      *path)
{
  g_autoptr(GError) error = NULL;

  dex_return_error_if_fail (!connection || G_IS_DBUS_CONNECTION (connection));
  dex_return_error_if_fail (path != NULL);

  if (connection != NULL)
    {
      g_autoptr(GVariant) reply = NULL;
      g_autoptr(GBytes) bytes = NULL;
      const char *contents = NULL;

      if (!(reply = dex_await_variant (dex_dbus_connection_call (connection,
                                                                 "org.gnome.Sysprof3",
                                                                 "/org/gnome/Sysprof3",
                                                                 "org.gnome.Sysprof3.Service",
                                                                 "GetProcFile",
                                                                 g_variant_new ("(^ay)", path),
                                                                 G_VARIANT_TYPE ("(ay)"),
                                                                 G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                                 G_MAXINT),
                                       &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));

      g_variant_get (reply, "(^&ay)", &contents);

      g_assert (contents != NULL);

      return dex_future_new_take_boxed (G_TYPE_BYTES,
                                        g_bytes_new_with_free_func (contents, strlen (contents),
                                                                    (GDestroyNotify) g_variant_unref,
                                                                    g_steal_pointer (&reply)));
    }
  else
    {
      g_autoptr(GFile) file = g_file_new_for_path (path);
      return dex_file_load_contents_bytes (file);
    }
}
