/* test-list-processes.c
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
      g_autoptr(GListModel) memory_maps = sysprof_document_process_list_memory_maps (process);
      g_autoptr(GListModel) mounts = sysprof_document_process_list_mounts (process);
      g_autoptr(GListModel) threads = sysprof_document_process_list_threads (process);
      guint n_maps;
      guint n_mounts;
      guint n_threads;

      if (pid != -1 && pid != sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)))
        continue;

      g_print ("%d: %s (Duration %lf seconds)\n",
               sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)),
               sysprof_document_process_get_command_line (process),
               sysprof_document_process_get_duration (process) / (double)SYSPROF_NSEC_PER_SEC);

      g_print ("  Address Layout:\n");
      n_maps = g_list_model_get_n_items (memory_maps);
      for (guint j = 0; j < n_maps; j++)
        {
          g_autoptr(SysprofDocumentMmap) map = g_list_model_get_item (memory_maps, j);

          g_print ("    [0x%"G_GINT64_MODIFIER"x:0x%"G_GINT64_MODIFIER"x] %s\n",
                   sysprof_document_mmap_get_start_address (map),
                   sysprof_document_mmap_get_end_address (map),
                   sysprof_document_mmap_get_file (map));
        }

      g_print ("  Mounts:\n");
      n_mounts = g_list_model_get_n_items (mounts);
      for (guint j = 0; j < n_mounts; j++)
        {
          g_autoptr(SysprofMount) mount = g_list_model_get_item (mounts, j);
          g_autofree char *subvol = sysprof_mount_get_superblock_option (mount, "subvol");

          g_print ("    %d %d %d:%d %s %s %s %s %s\n",
                   sysprof_mount_get_mount_id (mount),
                   sysprof_mount_get_parent_mount_id (mount),
                   sysprof_mount_get_device_major (mount),
                   sysprof_mount_get_device_minor (mount),
                   sysprof_mount_get_root (mount),
                   sysprof_mount_get_mount_point (mount),
                   sysprof_mount_get_mount_source (mount),
                   sysprof_mount_get_filesystem_type (mount),
                   sysprof_mount_get_superblock_options (mount));

          if (subvol != NULL)
            g_print ("      Had subvol superblock option: %s\n", subvol);
        }

      g_print ("  Threads:\n");
      n_threads = g_list_model_get_n_items (threads);
      for (guint j = 0; j < n_threads; j++)
        {
          g_autoptr(SysprofThreadInfo) thread = g_list_model_get_item (threads, j);

          g_print ("    Thread-Id: %i%s\n",
                   sysprof_thread_info_get_thread_id (thread),
                   sysprof_thread_info_is_main_thread (thread) ? " [Main Thread]" : "");
        }
    }

  return 0;
}
