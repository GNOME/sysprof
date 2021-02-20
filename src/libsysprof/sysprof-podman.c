/* sysprof-podman.c
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

#define G_LOG_DOMAIN "sysprof-podman"

#include "config.h"

#include "sysprof-podman.h"

static const char *debug_dirs[] = {
  "/usr/lib/debug",
  "/usr/lib32/debug",
  "/usr/lib64/debug",
};

void
_sysprof_podman_debug_dirs (GPtrArray *dirs)
{
  g_autofree gchar *base_path = NULL;
  g_autoptr(GDir) dir = NULL;
  const gchar *name;

  g_assert (dirs != NULL);

  base_path = g_build_filename (g_get_user_data_dir (),
                                "containres",
                                "storage",
                                "overlay",
                                NULL);

  if (!(dir = g_dir_open (base_path, 0, NULL)))
    return;

  while ((name = g_dir_read_name (dir)))
    {
      for (guint i = 0; i < G_N_ELEMENTS (debug_dirs); i++)
        {
          g_autofree gchar *debug_path = g_build_filename (base_path, name, "diff", debug_dirs[i], NULL);
          if (g_file_test (debug_path, G_FILE_TEST_IS_DIR))
            g_ptr_array_add (dirs, g_steal_pointer (&debug_path));
        }
    }
}

gchar **
sysprof_podman_debug_dirs (void)
{
  GPtrArray *dirs = g_ptr_array_new ();
  _sysprof_podman_debug_dirs (dirs);
  g_ptr_array_add (dirs, NULL);
  return (gchar **)g_ptr_array_free (dirs, FALSE);
}
