/* test-print-file.c
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
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) files = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = NULL;

  if (argc < 3)
    {
      g_printerr ("usage: %s CAPTURE_FILE EMBEDDED_FILE_PATH\n", argv[0]);
      return 1;
    }

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to open capture: %s\n", error->message);
      return 1;
    }

  file = sysprof_document_lookup_file (document, argv[2]);

  if (file == NULL)
    {
      g_printerr ("File \"%s\" not found.\n", argv[2]);
      return 1;
    }

  bytes = sysprof_document_file_dup_bytes (file);

  write (STDOUT_FILENO,
         g_bytes_get_data (bytes, NULL),
         g_bytes_get_size (bytes));

  return 0;
}
