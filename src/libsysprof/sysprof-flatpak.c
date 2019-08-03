/* sysprof-flatpak.c
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

#include "config.h"

#include <sys/utsname.h>

#include "sysprof-flatpak.h"

#define ETC_INSTALLATIONS_D "/etc/flatpak/installations.d"

static void
add_from_installations_d (GPtrArray   *ret,
                          const gchar *path,
                          const gchar *prefix)
{
  g_autoptr(GDir) dir = NULL;

  g_assert (ret != NULL);
  g_assert (path != NULL);

  /* Now look at /etc/flatpak/installations.d for keyfiles with Path= */
  if ((dir = g_dir_open (path, 0, NULL)))
    {
      const gchar *name;

      while ((name = g_dir_read_name (dir)))
        {
          g_autofree gchar *key_path = g_build_filename (path, name, NULL);
          g_autoptr(GKeyFile) kf = g_key_file_new ();

          if (g_key_file_load_from_file (kf, key_path, G_KEY_FILE_NONE, NULL))
            {
              g_auto(GStrv) groups = g_key_file_get_groups (kf, NULL);

              for (guint i = 0; groups[i]; i++)
                {
                  if (g_key_file_has_key (kf, groups[i], "Path", NULL))
                    {
                      gchar *val = g_key_file_get_string (kf, groups[i], "Path", NULL);

                      if (val)
                        {
                          if (prefix)
                            g_ptr_array_add (ret, g_build_filename (prefix, val, NULL));
                          else
                            g_ptr_array_add (ret, g_steal_pointer (&val));
                        }
                    }
                }
            }
        }
    }
}

gchar **
get_installations (void)
{
  GPtrArray *ret = g_ptr_array_new ();

  /* We might be running from a container, so ignore XDG_DATA_HOME as
   * that will likely be different that what we care about the host.
   * TODO: Can we find a way to support non-standard XDG_DATA_HOME?
   */
  g_ptr_array_add (ret, g_build_filename (g_get_home_dir (), ".local", "share", "flatpak", NULL));
  g_ptr_array_add (ret, g_strdup ("/var/lib/flatpak"));

  add_from_installations_d (ret, ETC_INSTALLATIONS_D, NULL);
  add_from_installations_d (ret, "/var/run/host" ETC_INSTALLATIONS_D, "/var/run/host");

  g_ptr_array_add (ret, NULL);
  return (gchar **)g_ptr_array_free (ret, FALSE);
}

static void
get_arch (gchar *out,
          gsize  len)
{
  struct utsname u;
  uname (&u);
  g_strlcpy (out, u.machine, len);
}

void
_sysprof_flatpak_debug_dirs (GPtrArray *dirs)
{
  g_auto(GStrv) installs = get_installations ();
  gchar arch[32];

  g_assert (dirs != NULL);

  get_arch (arch, sizeof arch);

  g_ptr_array_add (dirs, g_strdup ("/var/run/host/usr/lib/debug"));
  g_ptr_array_add (dirs, g_strdup ("/var/run/host/usr/lib32/debug"));
  g_ptr_array_add (dirs, g_strdup ("/var/run/host/usr/lib64/debug"));

  /* For each of the installations, we want to look at all of the runtimes that
   * exist within it. Of those runtimes, we want to limit ourselves to the active
   * version of each runtime, and see if we have a deployment for the current
   * system arch that contains a "lib/debug" directory. We could add more, but
   * it's just too many directories.
   */

  for (guint i = 0; installs[i]; i++)
    {
      g_autofree gchar *repo_dir = g_build_filename (installs[i], "runtime", NULL);
      g_autoptr(GDir) dir = g_dir_open (repo_dir, 0, NULL);
      const gchar *name;

      if (dir == NULL)
        continue;

      while ((name = g_dir_read_name (dir)))
        {
          g_autofree gchar *version_dir = g_build_filename (installs[i], "runtime", name, arch, NULL);
          g_autoptr(GDir) vdir = g_dir_open (version_dir, 0, NULL);
          const gchar *version;

          if (vdir == NULL)
            continue;

          while ((version = g_dir_read_name (vdir)))
            {
              g_autofree gchar *lib_debug = g_build_filename (version_dir, version, "active", "files", "lib", "debug", NULL);

              if (g_file_test (lib_debug, G_FILE_TEST_EXISTS))
                g_ptr_array_add (dirs, g_steal_pointer (&lib_debug));
            }
        }
    }
}

gchar **
sysprof_flatpak_debug_dirs (void)
{
  GPtrArray *dirs = g_ptr_array_new ();
  _sysprof_flatpak_debug_dirs (dirs);
  g_ptr_array_add (dirs, NULL);
  return (gchar **)g_ptr_array_free (dirs, FALSE);
}
