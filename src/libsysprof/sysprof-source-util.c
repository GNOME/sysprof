/* sysprof-source-util.c
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

#include <gio/gio.h>
#include <string.h>

#include "sysprof-source-util-private.h"

gboolean
sysprof_host_file_get_contents (const gchar  *host_path,
                           gchar       **contents,
                           gsize        *len,
                           GError      **error)
{
  g_autofree gchar *alt_path = NULL;

  g_return_val_if_fail (host_path != NULL, FALSE);

  if (!g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS))
    return g_file_get_contents (host_path, contents, len, error);

  if (contents != NULL)
    *contents = NULL;

  if (len != NULL)
    *len = 0;

  alt_path = g_build_filename ("/var/run/host", host_path, NULL);

  if (g_file_test (alt_path, G_FILE_TEST_EXISTS))
    return g_file_get_contents (alt_path, contents, len, error);

  /*
   * Fallback to try to get it with "cat" on the host since we
   * may not have access (like for /proc) from /var/run/host.
   */

  {
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    g_autoptr(GSubprocess) subprocess = NULL;
    g_autoptr(GBytes) stdout_buf = NULL;

    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE);

    subprocess = g_subprocess_launcher_spawn (launcher, error,
                                              "flatpak-spawn",
                                              "--clear-env",
                                              "--host",
                                              "cat",
                                              host_path,
                                              NULL);

    if (subprocess == NULL)
      return FALSE;

    if (!g_subprocess_communicate (subprocess, NULL, NULL, &stdout_buf, NULL, error))
      return FALSE;

    if (len != NULL)
      *len = g_bytes_get_size (stdout_buf);

    if (contents != NULL)
      {
        const guint8 *data;
        gsize n;

        /* g_file_get_contents() gurantees a trailing null byte */
        data = g_bytes_get_data (stdout_buf, &n);
        *contents = g_malloc (n + 1);
        memcpy (*contents, data, n);
        (*contents)[n] = '\0';
      }
  }

  return TRUE;
}

gchar **
sysprof_host_list_directories (const gchar  *directory,
                          GError      **error)
{
  g_autofree gchar *alt_path = NULL;

  g_return_val_if_fail (directory != NULL, NULL);

  if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      g_autoptr(GSubprocessLauncher) launcher = NULL;
      g_autoptr(GSubprocess) subprocess = NULL;
      g_autofree gchar *stdout_buf = NULL;
      g_auto(GStrv) lines = NULL;
      gsize len;
      guint j = 0;

      alt_path = g_build_filename ("/var/run/host", directory, NULL);

      if (g_file_test (alt_path, G_FILE_TEST_IS_DIR))
        {
          directory = alt_path;
          goto try_native;
        }

      launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                            G_SUBPROCESS_FLAGS_STDERR_SILENCE);
      subprocess = g_subprocess_launcher_spawn (launcher, error,
                                                "flatpak-spawn",
                                                "--clear-env",
                                                "--host",
                                                "ls",
                                                "-1",
                                                "-U",
                                                "--file-type",
                                                directory,
                                                NULL);
      if (subprocess == NULL)
        return NULL;

      if (!g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &stdout_buf, NULL, error))
        return NULL;

      lines = g_strsplit (stdout_buf, "\n", 0);
      len = g_strv_length (lines);

      for (gsize i = 0; i < len; i++)
        {
          gsize llen = strlen (lines[i]);

          if (llen == 0 || lines[i][llen-1] != '/')
            {
              /* Remove this entry, we'll compress the list later */
              g_free (lines[i]);
              lines[i] = NULL;
            }
          else
            {
              /* trim trailing / */
              lines[i][llen-1] = 0;
            }
        }

      /* Compress the list by removing NULL links */
      for (gsize i = 0; i < len; i++)
        {
          if (lines[i] == NULL)
            {
              if (j <= i)
                j = i + 1;

              for (; j < len; j++)
                {
                  if (lines[j] != NULL)
                    {
                      lines[i] = g_steal_pointer (&lines[j]);
                      break;
                    }
                }
            }
        }

      return g_steal_pointer (&lines);
    }

try_native:

    {
      g_autoptr(GDir) dir = g_dir_open (directory, 0, error);
      g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
      const gchar *name;

      if (dir == NULL)
        return NULL;

      while ((name = g_dir_read_name (dir)))
        {
          g_autofree gchar *path = g_build_filename (directory, name, NULL);

          if (g_file_test (path, G_FILE_TEST_IS_DIR))
            g_ptr_array_add (dirs, g_steal_pointer (&path));
        }

      g_ptr_array_add (dirs, NULL);

      return (gchar **)g_ptr_array_free (g_steal_pointer (&dirs), FALSE);
    }
}
