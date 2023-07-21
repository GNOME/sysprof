/* sysprof-chart-layer-item.c
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

#include "sysprof-chart-layer-item.h"

struct _SysprofChartLayerItem
{
  GObject parent_instance;
  GObject *item;
  SysprofChartLayer *layer;
};

enum {
  PROP_0,
  PROP_ITEM,
  PROP_LAYER,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofChartLayerItem, sysprof_chart_layer_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_chart_layer_item_dispose (GObject *object)
{
  SysprofChartLayerItem *self = (SysprofChartLayerItem *)object;

  g_clear_object (&self->layer);
  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_chart_layer_item_parent_class)->dispose (object);
}

static void
sysprof_chart_layer_item_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofChartLayerItem *self = SYSPROF_CHART_LAYER_ITEM (object);

  switch (prop_id)
    {
    case PROP_LAYER:
      g_value_set_object (value, sysprof_chart_layer_item_get_layer (self));
      break;

    case PROP_ITEM:
      g_value_set_object (value, sysprof_chart_layer_item_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_item_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SysprofChartLayerItem *self = SYSPROF_CHART_LAYER_ITEM (object);

  switch (prop_id)
    {
    case PROP_LAYER:
      sysprof_chart_layer_item_set_layer (self, g_value_get_object (value));
      break;

    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_item_class_init (SysprofChartLayerItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_chart_layer_item_dispose;
  object_class->get_property = sysprof_chart_layer_item_get_property;
  object_class->set_property = sysprof_chart_layer_item_set_property;

  properties[PROP_LAYER] =
    g_param_spec_object ("layer", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_chart_layer_item_init (SysprofChartLayerItem *self)
{
}

SysprofChartLayer *
sysprof_chart_layer_item_get_layer (SysprofChartLayerItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_CHART_LAYER_ITEM (self), NULL);

  return self->layer;
}

void
sysprof_chart_layer_item_set_layer (SysprofChartLayerItem *self,
                                    SysprofChartLayer     *layer)
{
  g_return_if_fail (SYSPROF_IS_CHART_LAYER_ITEM (self));
  g_return_if_fail (!layer || SYSPROF_IS_CHART_LAYER (layer));

  if (g_set_object (&self->layer, layer))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

gpointer
sysprof_chart_layer_item_get_item (SysprofChartLayerItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_CHART_LAYER_ITEM (self), NULL);

  return self->item;
}
