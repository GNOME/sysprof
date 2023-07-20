/* test-list-address-layout.c
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
  int pid;

  if (argc < 3)
    {
      g_printerr ("usage: %s CAPTURE_FILE PID\n", argv[0]);
      return 1;
    }

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

      if (sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)) == pid)
        {
          g_autoptr(GListModel) memory_maps = sysprof_document_process_list_memory_maps (process);
          guint n_maps = g_list_model_get_n_items (memory_maps);

          for (guint j = 0; j < n_maps; j++)
            {
              g_autoptr(SysprofDocumentMmap) map = g_list_model_get_item (memory_maps, j);

              g_print ("    [0x%"G_GINT64_MODIFIER"x:0x%"G_GINT64_MODIFIER"x] %s <+0x%"G_GINT64_MODIFIER"x>\n",
                       sysprof_document_mmap_get_start_address (map),
                       sysprof_document_mmap_get_end_address (map),
                       sysprof_document_mmap_get_file (map),
                       sysprof_document_mmap_get_file_offset (map));
            }

          break;
        }
    }

  return 0;
}
