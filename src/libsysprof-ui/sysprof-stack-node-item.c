/* sysprof-stack-node-item.c
 *
 * Copyright 2023 Corentin Noël <corentin.noel@collabora.com>
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

#include "sysprof-stack-node-item.h"

#include "../stackstash.h"

struct _SysprofStackNodeItem
{
  GObject parent_instance;

  gdouble size;
  gdouble total;
  StackNode *node;
};

G_DEFINE_FINAL_TYPE (SysprofStackNodeItem, sysprof_stack_node_item, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_NAME,
  PROP_SIZE,
  PROP_TOTAL,
  PROP_NODE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_stack_node_item_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofStackNodeItem *self = SYSPROF_STACK_NODE_ITEM (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, U64_TO_POINTER(self->node->data));
      break;
    case PROP_SIZE:
      g_value_set_double (value, self->size);
      break;
    case PROP_TOTAL:
      g_value_set_double (value, self->total);
      break;
    case PROP_NODE:
      g_value_set_pointer (value, sysprof_stack_node_item_get_node (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_stack_node_item_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofStackNodeItem *self = SYSPROF_STACK_NODE_ITEM (object);

  switch (prop_id)
    {
    case PROP_SIZE:
      self->size = g_value_get_double (value);
      break;
    case PROP_TOTAL:
      self->total = g_value_get_double (value);
      break;
    case PROP_NODE:
      self->node = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_stack_node_item_class_init (SysprofStackNodeItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_stack_node_item_get_property;
  object_class->set_property = sysprof_stack_node_item_set_property;

  properties [PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "The name",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  properties [PROP_SIZE] =
    g_param_spec_double ("size",
                         "Size",
                         "The size",
                         0, G_MAXDOUBLE, 0,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  properties [PROP_TOTAL] =
    g_param_spec_double ("total",
                         "Total",
                         "The total",
                         0, G_MAXDOUBLE, 0,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  properties [PROP_NODE] =
    g_param_spec_pointer ("node",
                          "Node",
                          "The node",
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_stack_node_item_init (SysprofStackNodeItem *self)
{

}

SysprofStackNodeItem *
sysprof_stack_node_item_new (gpointer node, gdouble profile_size)
{
  const StackNode *n;
  guint size = 0;
  guint total = 0;

  for (n = node; n != NULL; n = n->next)
    {
      size += n->size;
      if (n->toplevel)
        total += n->total;
    }

  return g_object_new (SYSPROF_TYPE_STACK_NODE_ITEM,
                       "size", 100.0 * size / profile_size,
                       "total", 100.0 * total / profile_size,
                       "node", node,
                       NULL);
}

gpointer
sysprof_stack_node_item_get_node (SysprofStackNodeItem *self)
{
  g_return_val_if_fail(SYSPROF_IS_STACK_NODE_ITEM(self), NULL);

  return self->node;
}
