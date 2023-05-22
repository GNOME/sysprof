/* test-list-counters.c
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

  model = sysprof_document_list_counters (document);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (model, i);

      g_print ("%d<%s> %s.%s: %s\n",
               sysprof_document_counter_get_id (counter),
               sysprof_document_counter_get_value_type (counter) == G_TYPE_INT64 ? "i64" : "f64",
               sysprof_document_counter_get_category (counter),
               sysprof_document_counter_get_name (counter),
               sysprof_document_counter_get_description (counter));
    }

  return 0;
}
