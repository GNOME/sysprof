/* sysprof-descendants-model.c
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

#include "sysprof-allocator-private.h"
#include "sysprof-callgraph-private.h"
#include "sysprof-callgraph-frame-private.h"
#include "sysprof-descendants-model-private.h"
#include "sysprof-document-private.h"
#include "sysprof-symbol-private.h"

#define MAX_STACK_DEPTH 128

struct _SysprofDescendantsModel
{
  GObject               parent_instance;
  SysprofAllocator     *allocator;
  SysprofCallgraph     *callgraph;
  SysprofSymbol        *symbol;
  SysprofCallgraphNode  root;
};

static GType
sysprof_descendants_model_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_FRAME;
}

static guint
sysprof_descendants_model_get_n_items (GListModel *model)
{
  return 1;
}

static gpointer
sysprof_descendants_model_get_item (GListModel *model,
                                    guint       position)
{
  SysprofDescendantsModel *self = SYSPROF_DESCENDANTS_MODEL (model);

  if (position != 0)
    return NULL;

  return _sysprof_callgraph_frame_new_for_node (self->callgraph, G_OBJECT (self), &self->root);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_descendants_model_get_item_type;
  iface->get_n_items = sysprof_descendants_model_get_n_items;
  iface->get_item = sysprof_descendants_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDescendantsModel, sysprof_descendants_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_descendants_model_finalize (GObject *object)
{
  SysprofDescendantsModel *self = (SysprofDescendantsModel *)object;

  g_clear_object (&self->callgraph);
  g_clear_object (&self->symbol);

  g_clear_pointer (&self->allocator, sysprof_allocator_unref);

  G_OBJECT_CLASS (sysprof_descendants_model_parent_class)->finalize (object);
}

static void
sysprof_descendants_model_class_init (SysprofDescendantsModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_descendants_model_finalize;
}

static void
sysprof_descendants_model_init (SysprofDescendantsModel *self)
{
  self->allocator = sysprof_allocator_new ();
}

static SysprofCallgraphNode *
sysprof_descendants_model_add_trace (SysprofDescendantsModel  *self,
                                     SysprofSymbol           **symbols,
                                     guint                     n_symbols)
{
  SysprofCallgraphNode *parent = NULL;

  g_assert (SYSPROF_IS_DESCENDANTS_MODEL (self));
  g_assert (symbols != NULL);
  g_assert (n_symbols > 0);

  parent = &self->root;

  for (guint i = n_symbols; i > 0; i--)
    {
      SysprofSymbol *symbol = symbols[i-1];
      SysprofCallgraphNode *node = NULL;
      SysprofCallgraphSummary *summary;

      /* Try to find @symbol within the children of @parent */
      for (SysprofCallgraphNode *iter = parent->children;
           iter != NULL;
           iter = iter->next)
        {
          g_assert (iter != NULL);
          g_assert (iter->summary != NULL);
          g_assert (iter->summary->symbol != NULL);
          g_assert (symbol != NULL);

          if (_sysprof_symbol_equal (iter->summary->symbol, symbol))
            {
              node = iter;
              node->count++;
              goto next_symbol;
            }
        }

      if (!(summary = g_hash_table_lookup (self->callgraph->symbol_to_summary, symbol)))
        {
          node = parent;
          goto next_symbol;
        }

      /* Otherwise create a new node */
      node = sysprof_allocator_new0 (self->allocator, SysprofCallgraphNode);
      node->summary = summary;
      node->parent = parent;
      node->next = parent->children;
      node->count = 1;
      if (parent->children)
        parent->children->prev = node;
      parent->children = node;

      g_assert (node->summary != NULL);

    next_symbol:
      parent = node;
    }

  return parent;
}

static void
sysprof_descendants_model_add_traceable (SysprofDescendantsModel  *self,
                                         SysprofDocument          *document,
                                         SysprofDocumentTraceable *traceable,
                                         SysprofSymbol            *from_symbol,
                                         gboolean                  include_threads,
                                         gboolean                  merge_similar_processes)
{
  SysprofAddressContext final_context;
  SysprofSymbol **symbols;
  SysprofSymbolKind kind;
  guint stack_depth;
  guint n_symbols;

  g_assert (SYSPROF_IS_DESCENDANTS_MODEL (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));
  g_assert (SYSPROF_IS_SYMBOL (from_symbol));

  stack_depth = MIN (MAX_STACK_DEPTH, sysprof_document_traceable_get_stack_depth (traceable));
  symbols = g_alloca (sizeof (SysprofSymbol *) * (stack_depth + 2));
  n_symbols = sysprof_document_symbolize_traceable (document, traceable, symbols, stack_depth, &final_context);

  kind = sysprof_symbol_get_kind (from_symbol);

  if (kind == SYSPROF_SYMBOL_KIND_PROCESS || kind == SYSPROF_SYMBOL_KIND_THREAD)
    {
      int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));

      if (include_threads)
        {
          int thread_id = sysprof_document_traceable_get_thread_id (traceable);
          symbols[n_symbols++] = _sysprof_document_thread_symbol (document, pid, thread_id);
        }

      symbols[n_symbols++] = _sysprof_document_process_symbol (document, pid, merge_similar_processes);
    }

  for (guint i = n_symbols; i > 0; i--)
    {
      SysprofSymbol *symbol = symbols[i-1];

      n_symbols--;

      if (_sysprof_symbol_equal (symbol, from_symbol))
        break;
    }

  if (n_symbols > 0)
    {
      SysprofCallgraphNode *node;

      node = sysprof_descendants_model_add_trace (self, symbols, n_symbols);

      node->is_toplevel = TRUE;

      if (node && self->callgraph->augment_func)
        self->callgraph->augment_func (self->callgraph,
                                       node,
                                       SYSPROF_DOCUMENT_FRAME (traceable),
                                       FALSE,
                                       self->callgraph->augment_func_data);

      if ((self->callgraph->flags & SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES) != 0)
        _sysprof_callgraph_categorize (self->callgraph, node);
    }
}

GListModel *
_sysprof_descendants_model_new (SysprofCallgraph *callgraph,
                                SysprofSymbol    *symbol)
{
  SysprofDescendantsModel *self;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) model = NULL;
  gboolean include_threads;
  gboolean merge_similar_processes;
  guint n_items;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (callgraph), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (symbol), NULL);

  model = sysprof_callgraph_list_traceables_for_symbol (callgraph, symbol);
  document = g_object_ref (callgraph->document);

  self = g_object_new (SYSPROF_TYPE_DESCENDANTS_MODEL, NULL);
  self->callgraph = g_object_ref (callgraph);
  self->symbol = g_object_ref (symbol);
  self->root.summary = g_hash_table_lookup (callgraph->symbol_to_summary, symbol);

  g_assert (self->root.summary != NULL);
  g_assert (_sysprof_symbol_equal (self->root.summary->symbol, symbol));

  include_threads = (callgraph->flags & SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS) != 0;
  merge_similar_processes = (callgraph->flags & SYSPROF_CALLGRAPH_FLAGS_MERGE_SIMILAR_PROCESSES) != 0;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (model, i);

      sysprof_descendants_model_add_traceable (self,
                                               document,
                                               traceable,
                                               symbol,
                                               include_threads,
                                               merge_similar_processes);
    }

  return G_LIST_MODEL (self);
}
