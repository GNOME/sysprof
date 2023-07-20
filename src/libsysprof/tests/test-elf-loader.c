/* test-elf-loader.c
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

#include "sysprof-document-process-private.h"
#include "sysprof-elf-loader-private.h"

static const GOptionEntry entries[] = {
  { 0 }
};

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test loading of ELF files");
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofElfLoader) elf_loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) processes = NULL;
  g_autoptr(GError) error = NULL;
  guint n_processes;

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    g_error ("%s", error->message);

  if (argc < 2)
    g_error ("usage: %s CAPTURE_FILE", argv[0]);

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  processes = sysprof_document_list_processes (document);
  n_processes = g_list_model_get_n_items (processes);

  elf_loader = sysprof_elf_loader_new ();

  for (guint i = 0; i < n_processes; i++)
    {
      g_autoptr(SysprofDocumentProcess) process = g_list_model_get_item (processes, i);
      g_autoptr(GListModel) memory_maps = sysprof_document_process_list_memory_maps (process);
      guint n_memory_maps = g_list_model_get_n_items (memory_maps);
      SysprofProcessInfo *info = _sysprof_document_process_get_info (process);

      for (guint j = 0; j < n_memory_maps; j++)
        {
          g_autoptr(SysprofDocumentMmap) memory_map = g_list_model_get_item (memory_maps, j);
          const char *file = sysprof_document_mmap_get_file (memory_map);
          const char *build_id = sysprof_document_mmap_get_build_id (memory_map);
          guint64 file_inode = sysprof_document_mmap_get_file_inode (memory_map);
          g_autoptr(SysprofElf) elf = sysprof_elf_loader_load (elf_loader, info->mount_namespace, file, build_id, file_inode, &error);

          if (elf == NULL)
            g_print ("%u: %s (build-id %s) (inode %"G_GINT64_FORMAT") => [unresolved]\n",
                     info->pid, file, build_id ? build_id : "none", file_inode);
          else
            g_print ("%u: %s => %s\n", info->pid, file, sysprof_elf_get_file (elf));

          g_clear_error (&error);
        }
    }

  return 0;
}
