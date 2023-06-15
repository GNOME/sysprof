/* test-mark-catalog.c
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

#include "sysprof-document-private.h"

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) marks = NULL;
  g_autoptr(GError) error = NULL;
  guint n_groups;

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

  marks = sysprof_document_catalog_marks (document);
  n_groups = g_list_model_get_n_items (marks);

  for (guint i = 0; i < n_groups; i++)
    {
      g_autoptr(SysprofMarkCatalog) catalog = g_list_model_get_item (marks, i);
      const char *group = sysprof_mark_catalog_get_name (catalog);
      guint n_names = g_list_model_get_n_items (G_LIST_MODEL (catalog));

      g_assert (SYSPROF_IS_MARK_CATALOG (catalog));
      g_assert (G_IS_LIST_MODEL (catalog));

      g_print ("%s\n", group);

      for (guint j = 0; j < n_names; j++)
        {
          g_autoptr(SysprofMarkCatalog) name_catalog = g_list_model_get_item (G_LIST_MODEL (catalog), j);
          const char *name = sysprof_mark_catalog_get_name (name_catalog);
          guint n_marks = g_list_model_get_n_items (G_LIST_MODEL (name_catalog));

          g_assert (SYSPROF_IS_MARK_CATALOG (catalog));
          g_assert (G_IS_LIST_MODEL (catalog));

          g_print ("  %s\n", name);

          for (guint k = 0; k < n_marks; k++)
            {
              g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (G_LIST_MODEL (name_catalog), k);

              g_assert (SYSPROF_IS_DOCUMENT_MARK (mark));

              g_assert_cmpstr (sysprof_document_mark_get_group (mark), ==, group);
              g_assert_cmpstr (sysprof_document_mark_get_name (mark), ==, name);

              g_print ("    %s\n", sysprof_document_mark_get_message (mark));
            }
        }
    }

  return 0;
}
