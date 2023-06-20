/*
 * sysprof-line-layer.c
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

#include "sysprof-line-layer.h"

struct _SysprofLineLayer
{
  SysprofChartLayer parent_instance;
  SysprofXYSeries *series;
  GdkRGBA color;
  guint use_curves : 1;
  guint flip_y : 1;
  guint fill : 1;
};

G_DEFINE_FINAL_TYPE (SysprofLineLayer, sysprof_line_layer, SYSPROF_TYPE_CHART_LAYER)

enum {
  PROP_0,
  PROP_COLOR,
  PROP_FILL,
  PROP_FLIP_Y,
  PROP_SERIES,
  PROP_USE_CURVES,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

SysprofChartLayer *
sysprof_line_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_LINE_LAYER, NULL);
}

static void
sysprof_line_layer_snapshot (GtkWidget   *widget,
                             GtkSnapshot *snapshot)
{
  SysprofLineLayer *self = (SysprofLineLayer *)widget;
  const SysprofXYSeriesValue *values;
  cairo_t *cr;
  double last_x;
  double last_y;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_LINE_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (width == 0 || height == 0 || self->series == NULL || self->color.alpha == 0)
    return;

  if (!(values = sysprof_xy_series_get_values (self->series, &n_values)))
    return;

  cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));

  cairo_set_line_width (cr, 1./width);

  if (!self->flip_y)
    cairo_set_matrix (cr, &(cairo_matrix_t) {1, 0, 0, -1, 0, height});

  cairo_scale (cr, width, height);

  last_x = values->x;
  last_y = values->y;


  if (self->fill)
    {
      cairo_move_to (cr, last_x, 0);
      cairo_line_to (cr, last_x, last_y);
    }
  else
    {
      cairo_move_to (cr, last_x, last_y);
    }

  if (self->use_curves)
    {
      for (guint i = 1; i < n_values; i++)
        {
          const SysprofXYSeriesValue *v = &values[i];
          double x = v->x;
          double y = v->y;

          cairo_curve_to (cr,
                          last_x + ((x - last_x)/2),
                          last_y,
                          last_x + ((x - last_x)/2),
                          y,
                          x,
                          y);

          last_x = x;
          last_y = y;
        }
    }
  else
    {
      for (guint i = 1; i < n_values; i++)
        {
          const SysprofXYSeriesValue *v = &values[i];

          cairo_line_to (cr, v->x, v->y);

          last_x = v->x;
          last_y = v->y;
        }
    }

  if (self->fill)
    {
      GdkRGBA fill_color = self->color;

      cairo_line_to (cr, last_x, 0);

      fill_color.alpha *= .5;
      cairo_fill_preserve (cr);
    }

  gdk_cairo_set_source_rgba (cr, &self->color);
  cairo_stroke (cr);

  cairo_destroy (cr);
}

static void
sysprof_line_layer_finalize (GObject *object)
{
  SysprofLineLayer *self = (SysprofLineLayer *)object;

  g_clear_pointer (&self->series, sysprof_xy_series_unref);

  G_OBJECT_CLASS (sysprof_line_layer_parent_class)->finalize (object);
}

static void
sysprof_line_layer_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofLineLayer *self = SYSPROF_LINE_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, sysprof_line_layer_get_color (self));
      break;

    case PROP_FILL:
      g_value_set_boolean (value, sysprof_line_layer_get_fill (self));
      break;

    case PROP_FLIP_Y:
      g_value_set_boolean (value, sysprof_line_layer_get_flip_y (self));
      break;

    case PROP_SERIES:
      g_value_set_boxed (value, sysprof_line_layer_get_series (self));
      break;

    case PROP_USE_CURVES:
      g_value_set_boolean (value, sysprof_line_layer_get_use_curves (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_line_layer_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofLineLayer *self = SYSPROF_LINE_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      sysprof_line_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_FILL:
      sysprof_line_layer_set_fill (self, g_value_get_boolean (value));
      break;

    case PROP_FLIP_Y:
      sysprof_line_layer_set_flip_y (self, g_value_get_boolean (value));
      break;

    case PROP_SERIES:
      sysprof_line_layer_set_series (self, g_value_get_boxed (value));
      break;

    case PROP_USE_CURVES:
      sysprof_line_layer_set_use_curves (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_line_layer_class_init (SysprofLineLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_line_layer_finalize;
  object_class->get_property = sysprof_line_layer_get_property;
  object_class->set_property = sysprof_line_layer_set_property;

  widget_class->snapshot = sysprof_line_layer_snapshot;

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILL] =
    g_param_spec_boolean ("fill", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FLIP_Y] =
    g_param_spec_boolean ("flip-y", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIES] =
    g_param_spec_boxed ("series", NULL, NULL,
                        SYSPROF_TYPE_XY_SERIES,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_USE_CURVES] =
    g_param_spec_boolean ("use-curves", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_line_layer_init (SysprofLineLayer *self)
{
  self->color.alpha = 1;
}

const GdkRGBA *
sysprof_line_layer_get_color (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), NULL);

  return &self->color;
}

void
sysprof_line_layer_set_color (SysprofLineLayer *self,
                              const GdkRGBA    *color)
{
  static const GdkRGBA black = {0,0,0,1};

  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  if (color == NULL)
    color = &black;

  if (!gdk_rgba_equal (&self->color, color))
    {
      self->color = *color;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

/**
 * sysprof_line_layer_get_series:
 * @self: a #SysprofLineLayer
 *
 *
 * Returns: (transfer none) (nullable): a #SysprofXYSeries or %NULL
 */
SysprofXYSeries *
sysprof_line_layer_get_series (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), NULL);

  return self->series;
}

void
sysprof_line_layer_set_series (SysprofLineLayer *self,
                               SysprofXYSeries  *series)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  if (series == self->series)
    return;

  g_clear_pointer (&self->series, sysprof_xy_series_unref);
  self->series = series ? sysprof_xy_series_ref (series) : NULL;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

gboolean
sysprof_line_layer_get_fill (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->fill;
}

void
sysprof_line_layer_set_fill (SysprofLineLayer *self,
                             gboolean          fill)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  fill = !!fill;

  if (fill != self->fill)
    {
      self->fill = fill;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

gboolean
sysprof_line_layer_get_flip_y (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->flip_y;
}

void
sysprof_line_layer_set_flip_y (SysprofLineLayer *self,
                               gboolean          flip_y)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  flip_y = !!flip_y;

  if (flip_y != self->flip_y)
    {
      self->flip_y = flip_y;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_Y]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

gboolean
sysprof_line_layer_get_use_curves (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->use_curves;
}

void
sysprof_line_layer_set_use_curves (SysprofLineLayer *self,
                                   gboolean          use_curves)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  use_curves = !!use_curves;

  if (use_curves != self->use_curves)
    {
      self->use_curves = use_curves;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USE_CURVES]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
