/* sysprof-chart-layer-item-widget.c
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

#include "sysprof-chart-layer-item-widget.h"

struct _SysprofChartLayerItemWidget
{
  SysprofChartLayer parent_instance;
  SysprofChartLayerItem *item;
  guint disposed : 1;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofChartLayerItemWidget, sysprof_chart_layer_item_widget, SYSPROF_TYPE_CHART_LAYER)

static GParamSpec *properties [N_PROPS];

static SysprofChartLayer *
get_layer (SysprofChartLayer *self)
{
  GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));

  return SYSPROF_CHART_LAYER (child);
}


static gpointer
sysprof_chart_layer_item_widget_lookup_item (SysprofChartLayer *self,
                                             double             x,
                                             double             y)
{
  SysprofChartLayer *layer;

  if (!(layer = get_layer (self)))
    return NULL;

  return sysprof_chart_layer_lookup_item (layer, x, y);
}

static void
sysprof_chart_layer_item_widget_snapshot_motion (SysprofChartLayer *self,
                                                 GtkSnapshot       *snapshot,
                                                 double             x,
                                                 double             y)
{
  SysprofChartLayer *layer;

  if (!(layer = get_layer (self)))
    return;

  sysprof_chart_layer_snapshot_motion (layer, snapshot, x, y);
}

static void
sysprof_chart_layer_item_widget_notify_layer_cb (SysprofChartLayerItemWidget *self,
                                                 GParamSpec                  *pspec,
                                                 SysprofChartLayerItem       *item)
{
  GtkWidget *child;

  g_assert (SYSPROF_IS_CHART_LAYER_ITEM_WIDGET (self));
  g_assert (SYSPROF_IS_CHART_LAYER_ITEM (item));

  if (self->disposed)
    return;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  if ((child = GTK_WIDGET (sysprof_chart_layer_item_get_layer (item))))
    gtk_widget_set_parent (child, GTK_WIDGET (self));
}

static void
sysprof_chart_layer_item_widget_constructed (GObject *object)
{
  SysprofChartLayerItemWidget *self = (SysprofChartLayerItemWidget *)object;
  SysprofChartLayer *layer;

  G_OBJECT_CLASS (sysprof_chart_layer_item_widget_parent_class)->constructed (object);

  g_return_if_fail (self->item != NULL);

  if ((layer = sysprof_chart_layer_item_get_layer (self->item)))
    gtk_widget_set_parent (GTK_WIDGET (layer), GTK_WIDGET (self));

  g_signal_connect_object (self->item,
                           "notify::layer",
                           G_CALLBACK (sysprof_chart_layer_item_widget_notify_layer_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
sysprof_chart_layer_item_widget_dispose (GObject *object)
{
  SysprofChartLayerItemWidget *self = (SysprofChartLayerItemWidget *)object;
  GtkWidget *child;

  self->disposed = TRUE;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_chart_layer_item_widget_parent_class)->dispose (object);
}

static void
sysprof_chart_layer_item_widget_get_property (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
  SysprofChartLayerItemWidget *self = SYSPROF_CHART_LAYER_ITEM_WIDGET (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, self->item);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_item_widget_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
  SysprofChartLayerItemWidget *self = SYSPROF_CHART_LAYER_ITEM_WIDGET (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_item_widget_class_init (SysprofChartLayerItemWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofChartLayerClass *chart_layer_class = SYSPROF_CHART_LAYER_CLASS (klass);

  object_class->constructed = sysprof_chart_layer_item_widget_constructed;
  object_class->dispose = sysprof_chart_layer_item_widget_dispose;
  object_class->get_property = sysprof_chart_layer_item_widget_get_property;
  object_class->set_property = sysprof_chart_layer_item_widget_set_property;

  chart_layer_class->lookup_item = sysprof_chart_layer_item_widget_lookup_item;
  chart_layer_class->snapshot_motion = sysprof_chart_layer_item_widget_snapshot_motion;

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_chart_layer_item_widget_init (SysprofChartLayerItemWidget *self)
{
}

SysprofChartLayerItemWidget *
sysprof_chart_layer_item_widget_new (SysprofChartLayerItem *item)
{
  g_return_val_if_fail (SYSPROF_IS_CHART_LAYER_ITEM (item), NULL);

  return g_object_new (SYSPROF_TYPE_CHART_LAYER_ITEM_WIDGET,
                       "item", item,
                       NULL);
}
