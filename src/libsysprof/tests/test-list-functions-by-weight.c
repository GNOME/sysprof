/* test-list-functions-by-weight.c
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

#include <sysprof.h>

#include "sysprof-callgraph-private.h"

typedef struct _Augment
{
  guint64 count;
} Augment;

static gboolean
seen_symbol (const SysprofCallgraphNode *node,
             const SysprofCallgraphNode *toplevel)
{
  for (const SysprofCallgraphNode *iter = toplevel;
       iter != node;
       iter = iter->parent)
    if (iter->summary == node->summary)
      return TRUE;
  return FALSE;
}

static void
augment_cb (SysprofCallgraph     *callgraph,
            SysprofCallgraphNode *node,
            SysprofDocumentFrame *frame,
            gboolean              summarize,
            gpointer              user_data)
{
  Augment *aug = sysprof_callgraph_get_summary_augment (callgraph, node);

  aug->count++;

  for (SysprofCallgraphNode *iter = node->parent; iter; iter = iter->parent)
    {
      if (!seen_symbol (iter, node))
        {
          aug = sysprof_callgraph_get_summary_augment (callgraph, iter);
          aug->count++;
        }
    }
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
  guint n_items;

  g_assert_no_error (error);
  g_assert_true (SYSPROF_IS_CALLGRAPH (callgraph));

  symbols = sysprof_callgraph_list_symbols (callgraph);
  n_items = g_list_model_get_n_items (symbols);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofCallgraphSymbol) symbol = g_list_model_get_item (symbols, i);
      Augment *aug = sysprof_callgraph_symbol_get_summary_augment (symbol);
      const char *name = sysprof_symbol_get_name (sysprof_callgraph_symbol_get_symbol (symbol));

      g_print ("%6"G_GINT64_FORMAT": %s\n", aug->count, name);
    }

  g_main_loop_quit (main_loop);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- list functions sorted by weight");
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;

  if (!g_option_context_parse (context, &argc, &argv, &error))
    g_error ("%s", error->message);

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE", argv[0]);
      return 1;
    }

  loader = sysprof_document_loader_new (argv[1]);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  samples = sysprof_document_list_samples (document);

  sysprof_document_callgraph_async (document,
                                    flags,
                                    samples,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    NULL,
                                    callgraph_cb,
                                    main_loop);

  g_main_loop_run (main_loop);

  return 0;
}
