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

#include <sysprof-analyze.h>

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) processes = NULL;
  g_autoptr(GError) error = NULL;
  guint n_items;

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  if (!(document = sysprof_document_new (argv[1], &error)))
    {
      g_printerr ("Failed to open capture: %s\n", error->message);
      return 1;
    }

  processes = sysprof_document_list_processes (document);
  n_items = g_list_model_get_n_items (processes);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentProcess) process = g_list_model_get_item (processes, i);

      g_print ("%d: %s\n",
               sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)),
               sysprof_document_process_get_command_line (process));
    }

  return 0;
}
