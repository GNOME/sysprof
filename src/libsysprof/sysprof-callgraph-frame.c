/* sysprof-callgraph-frame.c
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

#include <gio/gio.h>

#include "sysprof-callgraph-private.h"
#include "sysprof-callgraph-frame-private.h"
#include "sysprof-category-summary-private.h"
#include "sysprof-enums.h"
#include "sysprof-symbol-private.h"
#include "sysprof-document-bitset-index-private.h"

#include "eggbitset.h"

#define MAX_STACK_DEPTH 128

struct _SysprofCallgraphFrame
{
  GObject               parent_instance;
  SysprofCallgraph     *callgraph;
  GObject              *owner;
  SysprofCallgraphNode *node;
  guint                 n_children;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  PROP_CATEGORY,
  PROP_SYMBOL,
  PROP_N_ITEMS,
  N_PROPS
};

static GType
sysprof_callgraph_frame_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_FRAME;
}

static guint
sysprof_callgraph_frame_get_n_items (GListModel *model)
{
  return SYSPROF_CALLGRAPH_FRAME (model)->n_children;
}

static gpointer
sysprof_callgraph_frame_get_item (GListModel *model,
                                  guint       position)
{
  SysprofCallgraphFrame *self = SYSPROF_CALLGRAPH_FRAME (model);
  SysprofCallgraphNode *iter;

  if (self->callgraph == NULL)
    return NULL;

  iter = self->node->children;

  while (iter != NULL && position > 0)
    {
      iter = iter->next;
      position--;
    }

  if (iter == NULL)
    return NULL;

  return _sysprof_callgraph_frame_new_for_node (self->callgraph, self->owner, iter);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_callgraph_frame_get_item_type;
  iface->get_n_items = sysprof_callgraph_frame_get_n_items;
  iface->get_item = sysprof_callgraph_frame_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofCallgraphFrame, sysprof_callgraph_frame, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_callgraph_frame_finalize (GObject *object)
{
  SysprofCallgraphFrame *self = (SysprofCallgraphFrame *)object;

  g_clear_weak_pointer (&self->callgraph);
  g_clear_object (&self->owner);
  self->node = NULL;

  G_OBJECT_CLASS (sysprof_callgraph_frame_parent_class)->finalize (object);
}

static void
sysprof_callgraph_frame_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofCallgraphFrame *self = SYSPROF_CALLGRAPH_FRAME (object);

  switch (prop_id)
    {
    case PROP_CALLGRAPH:
      g_value_set_object (value, self->callgraph);
      break;

    case PROP_CATEGORY:
      g_value_set_enum (value, sysprof_callgraph_frame_get_category (self));
      break;

    case PROP_N_ITEMS:
      g_value_set_uint (value, g_list_model_get_n_items (G_LIST_MODEL (self)));
      break;

    case PROP_SYMBOL:
      g_value_set_object (value, sysprof_callgraph_frame_get_symbol (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_frame_class_init (SysprofCallgraphFrameClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_callgraph_frame_finalize;
  object_class->get_property = sysprof_callgraph_frame_get_property;

  properties [PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CATEGORY] =
    g_param_spec_enum ("category", NULL, NULL,
                       SYSPROF_TYPE_CALLGRAPH_CATEGORY,
                       SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_N_ITEMS] =
    g_param_spec_uint ("n-items", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SYMBOL] =
    g_param_spec_object ("symbol", NULL, NULL,
                         SYSPROF_TYPE_SYMBOL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_callgraph_frame_init (SysprofCallgraphFrame *self)
{
}

SysprofCallgraphFrame *
_sysprof_callgraph_frame_new_for_node (SysprofCallgraph     *callgraph,
                                       GObject              *owner,
                                       SysprofCallgraphNode *node)
{
  SysprofCallgraphFrame *self;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (callgraph), NULL);
  g_return_val_if_fail (node != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH_FRAME, NULL);
  g_set_weak_pointer (&self->callgraph, callgraph);
  g_set_object (&self->owner, owner);
  self->node = node;

  for (const SysprofCallgraphNode *iter = node->children;
       iter != NULL;
       iter = iter->next)
    self->n_children++;

  return self;
}

/**
 * sysprof_callgraph_frame_get_symbol:
 * @self: a #SysprofCallgraphFrame
 *
 * Gets the symbol for the frame.
 *
 * Returns: (nullable) (transfer none): a #SysprofSymbol
 */
SysprofSymbol *
sysprof_callgraph_frame_get_symbol (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);

  if (self->callgraph == NULL)
    return NULL;

  return self->node->summary->symbol;
}

/**
 * sysprof_callgraph_frame_get_augment: (skip)
 * @self: a #SysprofCallgraphFrame
 *
 * Gets the augmentation that was attached to the callgrpah node.
 *
 * Returns: (nullable) (transfer none): the augmentation data
 */
gpointer
sysprof_callgraph_frame_get_augment (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);

  if (self->callgraph == NULL)
    return NULL;

  return sysprof_callgraph_get_augment (self->callgraph, self->node);
}

/**
 * sysprof_callgraph_frame_get_summary_augment: (skip)
 * @self: a #SysprofCallgraphFrame
 *
 * Gets the augmentation that was attached to the summary for
 * the callgraph node's symbol.
 *
 * Returns: (nullable) (transfer none): the augmentation data
 */
gpointer
sysprof_callgraph_frame_get_summary_augment (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);

  if (self->callgraph == NULL)
    return NULL;

  return sysprof_callgraph_get_summary_augment (self->callgraph, self->node);
}

/**
 * sysprof_callgraph_frame_get_callgraph:
 * @self: a #SysprofCallgraphFrame
 *
 * Gets the callgraph the frame belongs to.
 *
 * Returns: (transfer none) (nullable): a #SysprofCallgraph, or %NULL
 */
SysprofCallgraph *
sysprof_callgraph_frame_get_callgraph (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);

  return self->callgraph;
}

static void
sysprof_callgraph_frame_list_traceables_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  SysprofCallgraph *callgraph = (SysprofCallgraph *)object;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(model = sysprof_callgraph_list_traceables_for_node_finish (callgraph, result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_steal_pointer (&model), g_object_unref);
}

/**
 * sysprof_callgraph_frame_list_traceables:
 * @self: a #SysprofCallgraphFrame
 * @cancellable: (nullable): a #GCancellable or %NULL
 * @callback: a #GAsyncReadyCallback
 * @user_data: closure data for @callback
 *
 * Asynchronously lists the traceables that contain @self.
 */
void
sysprof_callgraph_frame_list_traceables_async (SysprofCallgraphFrame *self,
                                               GCancellable          *cancellable,
                                               GAsyncReadyCallback    callback,
                                               gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_frame_list_traceables_async);

  if (self->callgraph == NULL)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Callgraph already disposed");
  else
    sysprof_callgraph_list_traceables_for_node_async (self->callgraph,
                                                      self->node,
                                                      cancellable,
                                                      sysprof_callgraph_frame_list_traceables_cb,
                                                      g_steal_pointer (&task));
}

/**
 * sysprof_callgraph_frame_list_traceables_finish:
 * @self: a #SysprofCallgraphFrame
 *
 * Completes an asynchronous request to list traceables.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentTraceable if
 *   successful; otherwise %NULL and @error is set.
 */
GListModel *
sysprof_callgraph_frame_list_traceables_finish (SysprofCallgraphFrame  *self,
                                                GAsyncResult           *result,
                                                GError                **error)
{
  GListModel *ret;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  ret = g_task_propagate_pointer (G_TASK (result), error);

  g_return_val_if_fail (!ret || G_IS_LIST_MODEL (ret), NULL);

  return ret;
}

gboolean
sysprof_callgraph_frame_is_leaf (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), 0);

  return self->n_children == 0;
}

/**
 * sysprof_callgraph_frame_get_category:
 * @self: a #SysprofCallgraphFrame
 *
 * Gets the category of the node if %SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES
 * was set when generating the callgraph. Otherwise
 * %SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED.
 *
 * Returns: callgraph category
 */
SysprofCallgraphCategory
sysprof_callgraph_frame_get_category (SysprofCallgraphFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), 0);

  if (self->callgraph != NULL && self->node != NULL && self->node->category)
    return self->node->category & ~SYSPROF_CALLGRAPH_CATEGORY_INHERIT;

  return SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;
}

typedef struct _Summary
{
  guint64 count;
} Summary;

static void
summarize_node (const SysprofCallgraphNode *node,
                Summary                    *summaries)
{
  guint category = SYSPROF_CALLGRAPH_CATEGORY_UNMASK(node->category);

  if (node->is_toplevel &&
      category != 0 &&
      category != SYSPROF_CALLGRAPH_CATEGORY_PRESENTATION)
    {
      gboolean seen[SYSPROF_CALLGRAPH_CATEGORY_LAST] = {0};

      /* Track total count in 0 */
      summaries[0].count += node->count;

      seen[category] = TRUE;
      summaries[category].count += node->count;

      for (const SysprofCallgraphNode *parent = node->parent; parent; parent = parent->parent)
        {
          guint parent_category = SYSPROF_CALLGRAPH_CATEGORY_UNMASK (parent->category);

          if (!seen[parent_category] && (parent->category & SYSPROF_CALLGRAPH_CATEGORY_INHERIT) != 0)
            {
              seen[parent_category] = TRUE;
              summaries[parent_category].count += node->count;
            }
        }
    }

  for (const SysprofCallgraphNode *iter = node->children; iter; iter = iter->next)
    summarize_node (iter, summaries);
}

static void
sysprof_callgraph_frame_summarize (GTask        *task,
                                   gpointer      source_object,
                                   gpointer      task_data,
                                   GCancellable *cancellable)
{
  SysprofCallgraphFrame *self = source_object;
  g_autofree Summary *summaries = NULL;
  g_autoptr(GListStore) store = NULL;
  guint64 total = 0;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_CALLGRAPH_FRAME (self));
  g_assert (SYSPROF_IS_CALLGRAPH (task_data));

  summaries = g_new0 (Summary, SYSPROF_CALLGRAPH_CATEGORY_LAST);
  summarize_node (self->node, summaries);

  store = g_list_store_new (G_TYPE_OBJECT);
  total = summaries[0].count;

  for (guint i = 1; i < SYSPROF_CALLGRAPH_CATEGORY_LAST; i++)
    {
      g_autoptr(SysprofCategorySummary) summary = NULL;

      if (summaries[i].count == 0)
        continue;

      summary = g_object_new (SYSPROF_TYPE_CATEGORY_SUMMARY, NULL);
      summary->category = i;
      summary->total = total;
      summary->count = summaries[i].count;

      g_list_store_append (store, summary);
    }

  g_task_return_pointer (task, g_steal_pointer (&store), g_object_unref);
}

void
sysprof_callgraph_frame_summarize_async (SysprofCallgraphFrame *self,
                                         GCancellable          *cancellable,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_frame_summarize_async);

  if (self->callgraph == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Callgraph disposed");
      return;
    }

  g_task_set_task_data (task, g_object_ref (self->callgraph), g_object_unref);
  g_task_run_in_thread (task, sysprof_callgraph_frame_summarize);
}

/**
 * sysprof_callgraph_frame_summarize_finish:
 *
 * Returns: (transfer full): a #GListModel of #SysprofCategorySummary
 */
GListModel *
sysprof_callgraph_frame_summarize_finish (SysprofCallgraphFrame  *self,
                                          GAsyncResult           *result,
                                          GError                **error)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_FRAME (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
