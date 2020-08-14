/* test-mountinfo.c
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

#include "sysprof-mountinfo.h"

gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(SysprofMountinfo) info = NULL;
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *lookup = NULL;
  gsize len;

  if (argc != 3)
    {
      g_printerr ("usage: %s MOUNTINFO_FILE PATH_TO_TRANSLATE\n", argv[0]);
      return 1;
    }

  info = sysprof_mountinfo_new ();

  if (!g_file_get_contents ("/proc/mounts", &contents, &len, &error))
    g_error ("%s", error->message);
  sysprof_mountinfo_parse_mounts (info, contents);
  g_free (g_steal_pointer (&contents));

  if (!g_file_get_contents (argv[1], &contents, &len, &error))
    g_error ("%s", error->message);
  sysprof_mountinfo_parse_mountinfo (info, contents);

  lookup = sysprof_mountinfo_translate (info, argv[2]);

  if (lookup)
    g_print ("%s\n", lookup);
  else
    g_print ("%s\n", argv[2]);

  return 0;
}
