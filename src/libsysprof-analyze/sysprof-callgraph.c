/* sysprof-callgraph.c
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

#include "sysprof-callgraph-private.h"
#include "sysprof-callgraph-frame-private.h"
#include "sysprof-document-private.h"
#include "sysprof-document-traceable.h"
#include "sysprof-symbol-private.h"

#define MAX_STACK_DEPTH 1024

struct _SysprofCallgraph
{
  GObject                  parent_instance;

  SysprofDocument         *document;
  GListModel              *traceables;

  SysprofSymbol           *everything;

  gsize                    augment_size;
  SysprofAugmentationFunc  augment_func;
  gpointer                 augment_func_data;
  GDestroyNotify           augment_func_data_destroy;

  SysprofCallgraphNode     root;
};

static GType
sysprof_callgraph_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_FRAME;
}

static guint
sysprof_callgraph_get_n_items (GListModel *model)
{
  return 1;
}

static gpointer
sysprof_callgraph_get_item (GListModel *model,
                            guint       position)
{
  SysprofCallgraph *self = SYSPROF_CALLGRAPH (model);

  if (position > 0)
    return NULL;

  return _sysprof_callgraph_frame_new (self, &self->root);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_callgraph_get_item_type;
  iface->get_n_items = sysprof_callgraph_get_n_items;
  iface->get_item = sysprof_callgraph_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofCallgraph, sysprof_callgraph, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_callgraph_dispose (GObject *object)
{
  SysprofCallgraph *self = (SysprofCallgraph *)object;
  GDestroyNotify notify = self->augment_func_data_destroy;
  gpointer notify_data = self->augment_func_data;

  self->augment_size = 0;
  self->augment_func = NULL;
  self->augment_func_data = NULL;
  self->augment_func_data_destroy = NULL;

  if (notify != NULL)
    notify (notify_data);

  G_OBJECT_CLASS (sysprof_callgraph_parent_class)->dispose (object);
}

static void
sysprof_callgraph_finalize (GObject *object)
{
  SysprofCallgraph *self = (SysprofCallgraph *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->traceables);
  g_clear_object (&self->everything);

  G_OBJECT_CLASS (sysprof_callgraph_parent_class)->finalize (object);
}

static void
sysprof_callgraph_class_init (SysprofCallgraphClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_callgraph_dispose;
  object_class->finalize = sysprof_callgraph_finalize;
}

static void
sysprof_callgraph_init (SysprofCallgraph *self)
{
  self->everything = _sysprof_symbol_new (g_ref_string_new_intern ("[Everything]"), NULL, NULL, 0, 0);
  self->root.symbol = self->everything;
}

static SysprofCallgraphNode *
sysprof_callgraph_add_trace (SysprofCallgraph  *self,
                             SysprofSymbol    **symbols,
                             guint              n_symbols)
{
  SysprofCallgraphNode *parent;

  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (n_symbols > 2);
  g_assert (symbols[n_symbols-1] == self->everything);

  parent = &self->root;

  for (guint i = n_symbols - 1; i > 0; i--)
    {
      SysprofSymbol *symbol = symbols[i-1];
      SysprofCallgraphNode *node = NULL;

      /* Try to find @symbol within the children of @parent */
      for (SysprofCallgraphNode *iter = parent->children;
           iter != NULL;
           iter = iter->next)
        {
          g_assert (iter != NULL);
          g_assert (iter->symbol != NULL);
          g_assert (symbol != NULL);

          if (_sysprof_symbol_equal (iter->symbol, symbol))
            {
              node = iter;
              goto next_symbol;
            }
        }

      /* Otherwise create a new node */
      node = g_new0 (SysprofCallgraphNode, 1);
      node->symbol = symbol;
      node->parent = parent;
      node->next = parent->children;
      if (parent->children)
        parent->children->prev = node;
      parent->children = node;

    next_symbol:
      parent = node;
    }

  return parent;
}

static void
sysprof_callgraph_add_traceable (SysprofCallgraph         *self,
                                 SysprofDocumentTraceable *traceable)
{
  SysprofCallgraphNode *node;
  SysprofSymbol **symbols;
  guint stack_depth;
  guint n_symbols;
  int pid;

  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

  pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));
  stack_depth = sysprof_document_traceable_get_stack_depth (traceable);

  if (stack_depth == 0 || stack_depth > MAX_STACK_DEPTH)
    return;

  symbols = g_newa (SysprofSymbol *, stack_depth + 2);
  n_symbols = sysprof_document_symbolize_traceable (self->document, traceable, symbols, stack_depth);

  g_assert (n_symbols > 0);
  g_assert (n_symbols <= stack_depth);

  /* We saved 2 extra spaces for these above so that we can
   * tack on the "Process" symbol and the "Everything" symbol.
   */
  symbols[n_symbols++] = _sysprof_document_process_symbol (self->document, pid);
  symbols[n_symbols++] = self->everything;

  node = sysprof_callgraph_add_trace (self, symbols, n_symbols);

  if (node && self->augment_func)
    self->augment_func (self,
                        node,
                        SYSPROF_DOCUMENT_FRAME (traceable),
                        self->augment_func_data);
}

static void
sysprof_callgraph_new_worker (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  SysprofCallgraph *self = task_data;
  guint n_items;

  g_assert (G_IS_TASK (task));
  g_assert (source_object == NULL);
  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  n_items = g_list_model_get_n_items (self->traceables);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (self->traceables, i);

      sysprof_callgraph_add_traceable (self, traceable);
    }

  g_task_return_pointer (task, g_object_ref (self), g_object_unref);
}

void
_sysprof_callgraph_new_async (SysprofDocument         *document,
                              GListModel              *traceables,
                              gsize                    augment_size,
                              SysprofAugmentationFunc  augment_func,
                              gpointer                 augment_func_data,
                              GDestroyNotify           augment_func_data_destroy,
                              GCancellable            *cancellable,
                              GAsyncReadyCallback      callback,
                              gpointer                 user_data)
{
  g_autoptr(SysprofCallgraph) self = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (G_IS_LIST_MODEL (traceables));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH, NULL);
  self->document = g_object_ref (document);
  self->traceables = g_object_ref (traceables);
  self->augment_size = augment_size;
  self->augment_func = augment_func;
  self->augment_func_data = augment_func_data;
  self->augment_func_data_destroy = augment_func_data_destroy;

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, _sysprof_callgraph_new_async);
  g_task_set_task_data (task, g_object_ref (self), g_object_unref);
  g_task_run_in_thread (task, sysprof_callgraph_new_worker);
}

SysprofCallgraph *
_sysprof_callgraph_new_finish (GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

gpointer
sysprof_callgraph_get_augment (SysprofCallgraph     *callgraph,
                               SysprofCallgraphNode *node)
{
  if (callgraph->augment_size == 0)
    return NULL;
  else if (callgraph->augment_size <= GLIB_SIZEOF_VOID_P)
    return &node->augment;

  if (node->augment == NULL)
    node->augment = g_malloc0 (callgraph->augment_size);

  return node->augment;
}

SysprofCallgraphNode *
sysprof_callgraph_node_parent (SysprofCallgraphNode *node)
{
  return node->parent;
}
