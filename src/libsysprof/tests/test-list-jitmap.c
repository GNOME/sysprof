/* test-list-jitmap.c
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
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;
  guint n_items;

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to open capture: %s\n", error->message);
      return 1;
    }

  model = sysprof_document_list_jitmaps (document);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentFrame) frame = g_list_model_get_item (model, i);

      if (SYSPROF_IS_DOCUMENT_JITMAP (frame))
        {
          SysprofDocumentJitmap *jitmap = SYSPROF_DOCUMENT_JITMAP (frame);
          guint size = sysprof_document_jitmap_get_size (jitmap);

          for (guint j = 0; j < size; j++)
            {
              SysprofAddress address;
              const char *name;

              if (!(name = sysprof_document_jitmap_get_mapping (jitmap, j, &address)))
                break;

              g_print ("0x%"G_GINT64_MODIFIER"x: %s\n", address, name);
            }
        }
    }

  return 0;
}
