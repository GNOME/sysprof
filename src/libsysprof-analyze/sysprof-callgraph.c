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
#include "sysprof-callgraph-frame.h"
#include "sysprof-document-traceable.h"

#define MAX_STACK_DEPTH 1024

struct _SysprofCallgraph
{
  GObject           parent_instance;
  SysprofDocument  *document;
  GListModel       *traceables;
};

typedef struct _SysprofCallgraphNode
{
  SysprofSymbol                *symbol;

  struct _SysprofCallgraphNode *next;
  struct _SysprofCallgraphNode *prev;

  struct _SysprofCallgraphNode *parent;
  struct _SysprofCallgraphNode *children;
} SysprofCallgraphNode;

typedef struct _SysprofCallgraphTrace
{
  guint                model_position;
  guint                n_nodes;
  SysprofCallgraphNode nodes[];
} SysprofCallgraphTrace;

static GType
sysprof_callgraph_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_FRAME;
}

static guint
sysprof_callgraph_get_n_items (GListModel *model)
{
  return 0;
}

static gpointer
sysprof_callgraph_get_item (GListModel *model,
                            guint       position)
{
  return NULL;
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
sysprof_callgraph_finalize (GObject *object)
{
  SysprofCallgraph *self = (SysprofCallgraph *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->traceables);

  G_OBJECT_CLASS (sysprof_callgraph_parent_class)->finalize (object);
}

static void
sysprof_callgraph_class_init (SysprofCallgraphClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_callgraph_finalize;
}

static void
sysprof_callgraph_init (SysprofCallgraph *self)
{
}

static void
sysprof_callgraph_trace_free (SysprofCallgraphTrace *trace)
{
  g_free (trace);
}

static void
sysprof_callgraph_add_trace (SysprofCallgraph      *self,
                             SysprofCallgraphTrace *trace)
{
  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (trace != NULL);

  sysprof_callgraph_trace_free (trace);
}

static void
sysprof_callgraph_add_traceable (SysprofCallgraph         *self,
                                 SysprofDocumentTraceable *traceable,
                                 guint                     model_position)
{
  SysprofCallgraphTrace *trace;
  SysprofSymbol **symbols;
  guint stack_depth;
  guint n_symbols;

  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

  stack_depth = sysprof_document_traceable_get_stack_depth (traceable);

  if (stack_depth > MAX_STACK_DEPTH)
    return;

  symbols = g_newa (SysprofSymbol *, stack_depth);
  n_symbols = sysprof_document_symbolize_traceable (self->document, traceable, symbols, stack_depth);

  if (n_symbols == 0)
    return;

  trace = g_malloc (sizeof *trace + (n_symbols * sizeof (SysprofCallgraphNode)));
  trace->model_position = model_position;
  trace->n_nodes = n_symbols;

  for (guint i = 0; i < n_symbols; i++)
    {
      trace->nodes[i].symbol = symbols[i];
      trace->nodes[i].children = NULL;
      trace->nodes[i].parent = NULL;
      trace->nodes[i].next = NULL;
      trace->nodes[i].prev = NULL;
    }

  sysprof_callgraph_add_trace (self, trace);
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

      sysprof_callgraph_add_traceable (self, traceable, i);
    }

  g_task_return_pointer (task, g_object_ref (self), g_object_unref);
}

void
_sysprof_callgraph_new_async (SysprofDocument     *document,
                              GListModel          *traceables,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(SysprofCallgraph) self = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (G_IS_LIST_MODEL (traceables));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH, NULL);
  self->document = g_object_ref (document);
  self->traceables = g_object_ref (traceables);

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
