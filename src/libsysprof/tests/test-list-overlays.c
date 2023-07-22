/* test-list-overlays.c
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

#include <sysprof.h>

#include "sysprof-document-private.h"

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader  = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) processes = NULL;
  g_autoptr(GError) error = NULL;
  guint n_items;
  int pid = -1;

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE [PID]\n", argv[0]);
      return 1;
    }

  if (argc == 3)
    pid = atoi (argv[2]);

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to open capture: %s\n", error->message);
      return 1;
    }

  processes = sysprof_document_list_processes (document);
  n_items = g_list_model_get_n_items (processes);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentProcess) process = g_list_model_get_item (processes, i);
      g_autoptr(GListModel) mounts = sysprof_document_process_list_mounts (process);
      g_autoptr(GString) str = g_string_new (NULL);
      guint n_mounts;

      if (pid != -1 && pid != sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)))
        continue;

      n_mounts = g_list_model_get_n_items (mounts);
      for (guint j = 0; j < n_mounts; j++)
        {
          g_autoptr(SysprofMount) mount = g_list_model_get_item (mounts, j);
          const char *fs_type = sysprof_mount_get_filesystem_type (mount);
          const char *value;

          if (g_strcmp0 (fs_type, "overlay") == 0)
            g_string_append_printf (str,
                                    "  @ID(%d) @ROOT(%s) @MOUNT_POINT(%s) @MOUNT_SOURCE(%s) @FS_TYPE(%s) @OPTIONS(%s)\n",
                                    sysprof_mount_get_mount_id (mount),
                                    sysprof_mount_get_root (mount),
                                    sysprof_mount_get_mount_point (mount),
                                    sysprof_mount_get_mount_source (mount),
                                    sysprof_mount_get_filesystem_type (mount),
                                    sysprof_mount_get_superblock_options (mount));

          if ((value = sysprof_mount_get_superblock_option (mount, "lowerdir")))
            {
              g_auto(GStrv) split = g_strsplit (value, ":", 0);

              g_string_append_printf (str, "      LOWER: %s\n", value);
              for (guint k = 0; split[k]; k++)
                g_string_append_printf (str, "        [%d]: %s\n", k, split[k]);
            }

          if ((value = sysprof_mount_get_superblock_option (mount, "upperdir")))
            g_string_append_printf (str, "      UPPER: %s\n", value);

          if ((value = sysprof_mount_get_superblock_option (mount, "workdir")))
            g_string_append_printf (str, "    WORKDIR: %s\n", value);
        }

      if (str->len > 0)
        g_print ("%d:\n%s",
                 sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)),
                 str->str);
    }

  return 0;
}
