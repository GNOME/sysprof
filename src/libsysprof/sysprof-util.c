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

/* Parse the kernel's sorted CPU list, preserving IDs when CPUs are offline. */
GArray *
_sysprof_parse_cpu_list (const char  *cpu_list,
                        GError     **error)
{
  g_autoptr(GArray) cpus = NULL;
  const char *p = cpu_list;

  g_return_val_if_fail (cpu_list != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  cpus = g_array_new (FALSE, FALSE, sizeof (guint));

  while (g_ascii_isspace (*p))
    p++;

  for (;;)
    {
      guint64 first;
      guint64 last;
      char *end;

      if (!g_ascii_isdigit (*p))
        goto invalid;

      first = g_ascii_strtoull (p, &end, 10);
      if (first > G_MAXINT)
        goto invalid;
      p = end;
      last = first;

      if (*p == '-')
        {
          p++;
          if (!g_ascii_isdigit (*p))
            goto invalid;
          last = g_ascii_strtoull (p, &end, 10);
          if (last > G_MAXINT || last < first)
            goto invalid;
          p = end;
        }

      if (cpus->len > 0 && first <= g_array_index (cpus, guint, cpus->len - 1))
        goto invalid;

      for (guint cpu = first; cpu <= last; cpu++)
        g_array_append_val (cpus, cpu);

      if (*p != ',')
        break;
      p++;
    }

  while (g_ascii_isspace (*p))
    p++;

  if (*p != 0)
    goto invalid;

  return g_steal_pointer (&cpus);

invalid:
  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_DATA,
                       "Invalid online CPU list");
  return NULL;
}

/* System-wide instruments must cover every online CPU, regardless of the
 * recording thread's affinity. g_get_num_processors() returns an affinity
 * count, which is neither a list of CPU IDs nor necessarily the host count.
 */
GArray *
_sysprof_get_online_cpus (GError **error)
{
  g_autofree char *contents = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!g_file_get_contents ("/sys/devices/system/cpu/online", &contents, NULL, error))
    return NULL;

  return _sysprof_parse_cpu_list (contents, error);
}
