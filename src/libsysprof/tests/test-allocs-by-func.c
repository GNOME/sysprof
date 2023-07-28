/* test-allocs-by-func.c
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

#include "config.h"

#include <glib/gi18n.h>
#include <locale.h>

#include <sysprof.h>

typedef struct _Augment
{
  guint64 total_allocs;
} Augment;

static int
sort_by_augment (gconstpointer a,
                 gconstpointer b)
{
  SysprofCallgraphSymbol *sym_a = *(SysprofCallgraphSymbol **)a;
  SysprofCallgraphSymbol *sym_b = *(SysprofCallgraphSymbol **)b;
  Augment *aug_a = sysprof_callgraph_symbol_get_summary_augment (sym_a);
  Augment *aug_b = sysprof_callgraph_symbol_get_summary_augment (sym_b);

  if (aug_a->total_allocs > aug_b->total_allocs)
    return -1;
  if (aug_a->total_allocs < aug_b->total_allocs)
    return 1;

  return 0;
}

static void
callgraph_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = SYSPROF_DOCUMENT (object);
  GMainLoop *main_loop = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCallgraph) callgraph = sysprof_document_callgraph_finish (document, result, &error);
  g_autoptr(GListModel) symbols = NULL;
  g_autoptr(GPtrArray) ar = NULL;
  guint n_items;

  g_assert_no_error (error);
  g_assert_true (SYSPROF_IS_CALLGRAPH (callgraph));

  symbols = sysprof_callgraph_list_symbols (callgraph);
  n_items = g_list_model_get_n_items (symbols);
  ar = g_ptr_array_new_with_free_func (g_object_unref);

  g_printerr ("Sorting by total allocation...\n");
  for (guint i = 0; i < n_items; i++)
    g_ptr_array_add (ar, g_list_model_get_item (symbols, i));
  g_ptr_array_sort (ar, sort_by_augment);

  g_printerr ("Listing...\n");

  for (guint i = 0; i < ar->len; i++)
    {
      SysprofCallgraphSymbol *sym = g_ptr_array_index (ar, i);
      SysprofSymbol *symbol = sysprof_callgraph_symbol_get_symbol (sym);
      Augment *aug = sysprof_callgraph_symbol_get_summary_augment (sym);
      g_autofree char *total_size = g_format_size (aug->total_allocs);

      g_print ("%10s : %s\n",
               total_size,
               sysprof_symbol_get_name (symbol));
    }

  g_main_loop_quit (main_loop);
}

static void
augment_cb (SysprofCallgraph     *callgraph,
            SysprofCallgraphNode *node,
            SysprofDocumentFrame *frame,
            gboolean              summarize,
            gpointer              user_data)
{
  Augment *aug;
  guint64 size;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_ALLOCATION (frame));
  g_assert (user_data == NULL);

  size = sysprof_document_allocation_get_size (SYSPROF_DOCUMENT_ALLOCATION (frame));
  if (size == 0)
    return;

  for (; node; node = sysprof_callgraph_node_parent (node))
    {
      aug = sysprof_callgraph_get_summary_augment (callgraph, node);
      aug->total_allocs += size;
    }
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) allocs = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  if (argc < 2)
    g_error ("usage: %s CAPTURE_FILE", argv[0]);

  g_printerr ("Loading document...\n");
  loader = sysprof_document_loader_new (argv[1]);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  allocs = sysprof_document_list_allocations (document);

  g_printerr ("Generating callgraph...\n");
  sysprof_document_callgraph_async (document,
                                    flags,
                                    allocs,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    NULL,
                                    callgraph_cb,
                                    main_loop);

  g_main_loop_run (main_loop);

  return 0;
}
