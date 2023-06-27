/* sysprof-duplex-layer.c
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

#include "sysprof-duplex-layer.h"

struct _SysprofDuplexLayer
{
  SysprofChartLayer  parent_instance;
  SysprofChartLayer *upper_layer;
  SysprofChartLayer *lower_layer;
};

enum {
  PROP_0,
  PROP_UPPER_LAYER,
  PROP_LOWER_LAYER,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDuplexLayer, sysprof_duplex_layer, SYSPROF_TYPE_CHART_LAYER)

static GParamSpec *properties [N_PROPS];

static void
sysprof_duplex_layer_dispose (GObject *object)
{
  SysprofDuplexLayer *self = (SysprofDuplexLayer *)object;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->upper_layer);
  g_clear_object (&self->lower_layer);

  G_OBJECT_CLASS (sysprof_duplex_layer_parent_class)->dispose (object);
}

static void
sysprof_duplex_layer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofDuplexLayer *self = SYSPROF_DUPLEX_LAYER (object);

  switch (prop_id)
    {
    case PROP_UPPER_LAYER:
      g_value_set_object (value, sysprof_duplex_layer_get_upper_layer (self));
      break;

    case PROP_LOWER_LAYER:
      g_value_set_object (value, sysprof_duplex_layer_get_lower_layer (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_duplex_layer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofDuplexLayer *self = SYSPROF_DUPLEX_LAYER (object);

  switch (prop_id)
    {
    case PROP_UPPER_LAYER:
      sysprof_duplex_layer_set_upper_layer (self, g_value_get_object (value));
      break;

    case PROP_LOWER_LAYER:
      sysprof_duplex_layer_set_lower_layer (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_duplex_layer_class_init (SysprofDuplexLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_duplex_layer_dispose;
  object_class->get_property = sysprof_duplex_layer_get_property;
  object_class->set_property = sysprof_duplex_layer_set_property;

  properties [PROP_UPPER_LAYER] =
    g_param_spec_object ("upper-layer", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_LOWER_LAYER] =
    g_param_spec_object ("lower-layer", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BOX_LAYOUT);
}

static void
sysprof_duplex_layer_init (SysprofDuplexLayer *self)
{
  g_object_set (gtk_widget_get_layout_manager (GTK_WIDGET (self)),
                "orientation", GTK_ORIENTATION_VERTICAL,
                NULL);
}

/**
 * sysprof_duplex_layer_get_upper_layer:
 * @self: a #SysprofDuplexLayer
 *
 * Returns: (transfer none) (nullable): the #SysprofChartLayer or %NULL
 */
SysprofChartLayer *
sysprof_duplex_layer_get_upper_layer (SysprofDuplexLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_DUPLEX_LAYER (self), NULL);

  return self->upper_layer;
}

void
sysprof_duplex_layer_set_upper_layer (SysprofDuplexLayer *self,
                                      SysprofChartLayer  *upper_layer)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_LAYER (self));
  g_return_if_fail (!upper_layer || SYSPROF_IS_CHART_LAYER (upper_layer));

  if (upper_layer == self->upper_layer)
    return;

  if (self->upper_layer)
    {
      gtk_widget_unparent (GTK_WIDGET (self->upper_layer));
      g_clear_object (&self->upper_layer);
    }

  g_set_object (&self->upper_layer, upper_layer);

  if (upper_layer)
    gtk_widget_insert_after (GTK_WIDGET (upper_layer), GTK_WIDGET (self), NULL);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UPPER_LAYER]);
}

/**
 * sysprof_duplex_layer_get_lower_layer:
 * @self: a #SysprofDuplexLayer
 *
 * Returns: (transfer none) (nullable): the #SysprofChartLayer or %NULL
 */
SysprofChartLayer *
sysprof_duplex_layer_get_lower_layer (SysprofDuplexLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_DUPLEX_LAYER (self), NULL);

  return self->lower_layer;
}

void
sysprof_duplex_layer_set_lower_layer (SysprofDuplexLayer *self,
                                      SysprofChartLayer  *lower_layer)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_LAYER (self));
  g_return_if_fail (!lower_layer || SYSPROF_IS_CHART_LAYER (lower_layer));

  if (lower_layer == self->lower_layer)
    return;

  if (self->lower_layer)
    {
      gtk_widget_unparent (GTK_WIDGET (self->lower_layer));
      g_clear_object (&self->lower_layer);
    }

  g_set_object (&self->lower_layer, lower_layer);

  if (lower_layer)
    gtk_widget_insert_before (GTK_WIDGET (lower_layer), GTK_WIDGET (self), NULL);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOWER_LAYER]);
}
