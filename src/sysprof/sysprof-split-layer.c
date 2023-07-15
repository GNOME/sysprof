/*
 * sysprof-split-layer.c
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

#include "sysprof-split-layer.h"

struct _SysprofSplitLayer
{
  SysprofChartLayer parent_instance;
  GtkWidget *top;
  GtkWidget *bottom;
};

G_DEFINE_FINAL_TYPE (SysprofSplitLayer, sysprof_split_layer, SYSPROF_TYPE_CHART_LAYER)

enum {
  PROP_0,
  PROP_BOTTOM,
  PROP_TOP,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_split_layer_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  g_assert (SYSPROF_IS_SPLIT_LAYER (widget));

  *minimum = *natural = 0;
  *minimum_baseline = *natural_baseline = -1;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      int m, n;

      gtk_widget_measure (child, orientation, for_size, &m, &n, NULL, NULL);

      if (m > *minimum)
        *minimum = m;

      if (n > *natural)
        *natural = n;
    }

  if (orientation == GTK_ORIENTATION_VERTICAL)
    {
      *minimum *= 2;
      *natural *= 2;
    }
}

static void
sysprof_split_layer_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
  SysprofSplitLayer *self = (SysprofSplitLayer *)widget;

  g_assert (SYSPROF_IS_SPLIT_LAYER (self));

  if (self->top != NULL)
    gtk_widget_size_allocate (self->top,
                              &(GtkAllocation) { 0, 0, width, floor (height / 2.) },
                              -1);

  if (self->bottom != NULL)
    gtk_widget_size_allocate (self->bottom,
                              &(GtkAllocation) { 0, ceil (height / 2.), width, floor (height / 2.) },
                              -1);
}

static void
sysprof_split_layer_finalize (GObject *object)
{
  SysprofSplitLayer *self = (SysprofSplitLayer *)object;

  g_clear_pointer (&self->top, gtk_widget_unparent);
  g_clear_pointer (&self->bottom, gtk_widget_unparent);

  G_OBJECT_CLASS (sysprof_split_layer_parent_class)->finalize (object);
}

static void
sysprof_split_layer_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofSplitLayer *self = SYSPROF_SPLIT_LAYER (object);

  switch (prop_id)
    {
    case PROP_TOP:
      g_value_set_object (value, sysprof_split_layer_get_top (self));
      break;

    case PROP_BOTTOM:
      g_value_set_object (value, sysprof_split_layer_get_bottom (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_split_layer_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofSplitLayer *self = SYSPROF_SPLIT_LAYER (object);

  switch (prop_id)
    {
    case PROP_TOP:
      sysprof_split_layer_set_top (self, g_value_get_object (value));
      break;

    case PROP_BOTTOM:
      sysprof_split_layer_set_bottom (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_split_layer_class_init (SysprofSplitLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_split_layer_finalize;
  object_class->get_property = sysprof_split_layer_get_property;
  object_class->set_property = sysprof_split_layer_set_property;

  widget_class->measure = sysprof_split_layer_measure;
  widget_class->size_allocate = sysprof_split_layer_size_allocate;

  properties [PROP_TOP] =
    g_param_spec_object ("top", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_BOTTOM] =
    g_param_spec_object ("bottom", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_split_layer_init (SysprofSplitLayer *self)
{
}

SysprofChartLayer *
sysprof_split_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_SPLIT_LAYER, NULL);
}

/**
 * sysprof_split_layer_get_top:
 *
 * Returns: (transfer none) (nullable): a #SysprofChartLayer or %NULL
 */
SysprofChartLayer *
sysprof_split_layer_get_top (SysprofSplitLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_SPLIT_LAYER (self), NULL);

  return SYSPROF_CHART_LAYER (self->top);
}

void
sysprof_split_layer_set_top (SysprofSplitLayer *self,
                             SysprofChartLayer *top)
{
  g_return_if_fail (SYSPROF_IS_SPLIT_LAYER (self));
  g_return_if_fail (!top || SYSPROF_IS_CHART_LAYER (top));

  if (top == SYSPROF_CHART_LAYER (self->top))
    return;

  if (self->top)
    gtk_widget_unparent (self->top);

  self->top = GTK_WIDGET (top);

  if (self->top)
    gtk_widget_set_parent (self->top, GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOP]);
}

/**
 * sysprof_split_layer_get_bottom:
 *
 * Returns: (transfer none) (nullable): a #SysprofChartLayer or %NULL
 */
SysprofChartLayer *
sysprof_split_layer_get_bottom (SysprofSplitLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_SPLIT_LAYER (self), NULL);

  return SYSPROF_CHART_LAYER (self->bottom);
}

void
sysprof_split_layer_set_bottom (SysprofSplitLayer *self,
                                SysprofChartLayer *bottom)
{
  g_return_if_fail (SYSPROF_IS_SPLIT_LAYER (self));
  g_return_if_fail (!bottom || SYSPROF_IS_CHART_LAYER (bottom));

  if (bottom == SYSPROF_CHART_LAYER (self->bottom))
    return;

  if (self->bottom)
    gtk_widget_unparent (self->bottom);

  self->bottom = GTK_WIDGET (bottom);

  if (self->bottom)
    gtk_widget_set_parent (self->bottom, GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOTTOM]);
}
