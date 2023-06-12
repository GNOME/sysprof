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

struct _SysprofCallgraphFrame
{
  GObject               parent_instance;
  SysprofCallgraph     *callgraph;
  SysprofCallgraphNode *node;
  guint                 n_children;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  PROP_SYMBOL,
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

  return _sysprof_callgraph_frame_new_for_node (self->callgraph, iter);
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
                                       SysprofCallgraphNode *node)
{
  SysprofCallgraphFrame *self;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (callgraph), NULL);
  g_return_val_if_fail (node != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH_FRAME, NULL);
  g_set_weak_pointer (&self->callgraph, callgraph);
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
