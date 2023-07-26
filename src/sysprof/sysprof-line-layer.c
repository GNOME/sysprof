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

#include <math.h>

#include "sysprof-chart-layer-private.h"
#include "sysprof-line-layer.h"
#include "sysprof-xy-layer-private.h"

#define NEAR_DISTANCE 50

struct _SysprofLineLayer
{
  SysprofXYLayer parent_instance;

  GdkRGBA color;

  guint antialias : 1;
  guint color_set : 1;
  guint dashed : 1;
  guint fill : 1;
  guint spline : 1;
};

struct _SysprofLineLayerClass
{
  SysprofXYLayerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofLineLayer, sysprof_line_layer, SYSPROF_TYPE_XY_LAYER)

enum {
  PROP_0,
  PROP_ANTIALIAS,
  PROP_COLOR,
  PROP_DASHED,
  PROP_FILL,
  PROP_SPLINE,
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
  const double *x_values;
  const double *y_values;
  const GdkRGBA *color;
  cairo_t *cr;
  double first_x;
  double first_y;
  double last_x;
  double last_y;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_LINE_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  _sysprof_xy_layer_get_xy (SYSPROF_XY_LAYER (self), &x_values, &y_values, &n_values);

  if (width == 0 || height == 0 || n_values == 0 || self->color.alpha == 0)
    return;

  if (self->color_set)
    color = &self->color;
  else
    color = _sysprof_chart_layer_get_accent_bg_color ();

  cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (0, -1, width, height+2));

  if (self->antialias)
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_GRAY);
  else
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);

  cairo_set_line_width (cr, .5);

  cairo_set_matrix (cr, &(cairo_matrix_t) {1, 0, 0, -1, 0, height});

  first_x = last_x = floor (x_values[0] * width);
  first_y = last_y = floor (y_values[0] * height) + .5;

  cairo_move_to (cr, first_x, first_y);

  if (self->spline)
    {
      for (guint i = 1; i < n_values; i++)
        {
          double x = floor (x_values[i] * width);
          double y = floor (y_values[i] * height) + .5;

          /* Skip if we are getting data incorrectly on the X axis.
           * It should have been sorted by this point.
           */
          if (x < last_x)
            continue;

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
          double x = floor (x_values[i] * width);
          double y = floor (y_values[i] * height) + .5;

          /* Skip if we are getting data incorrectly on the X axis.
           * It should have been sorted by this point.
           */
          if (x < last_x)
            continue;

          cairo_line_to (cr, x, y);

          last_x = x;
          last_y = y;
        }
    }

  if (self->dashed)
    cairo_set_dash (cr, (double[]){2}, 1, 0);
  gdk_cairo_set_source_rgba (cr, color);
  cairo_stroke_preserve (cr);

  if (self->fill)
    {
      GdkRGBA fill_color = *color;

      fill_color.alpha *= .25;
      gdk_cairo_set_source_rgba (cr, &fill_color);

      if (sysprof_xy_layer_get_flip_y (SYSPROF_XY_LAYER (self)))
        {
          cairo_line_to (cr, last_x, height-.5);
          cairo_line_to (cr, first_x, height-.5);
        }
      else
        {
          cairo_line_to (cr, last_x, .5);
          cairo_line_to (cr, first_x, .5);
        }

      cairo_line_to (cr, first_x, first_y);
      cairo_fill (cr);
    }

  cairo_destroy (cr);
}

static void
sysprof_line_layer_snapshot_motion (SysprofChartLayer *layer,
                                    GtkSnapshot       *snapshot,
                                    double             x,
                                    double             y)
{
  SysprofLineLayer *self = (SysprofLineLayer *)layer;
  const GdkRGBA *color;
  const double *x_values;
  const double *y_values;
  double best_distance = G_MAXDOUBLE;
  guint best_index = GTK_INVALID_LIST_POSITION;
  double best_x = 0;
  double best_y = 0;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_LINE_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  _sysprof_xy_layer_get_xy (SYSPROF_XY_LAYER (self), &x_values, &y_values, &n_values);

  if (width == 0 || height == 0 || n_values == 0)
    return;

  if (self->color_set)
    color = &self->color;
  else
    color = _sysprof_chart_layer_get_accent_bg_color ();

  if (color->alpha == .0)
    return;

  for (guint i = 0; i < n_values; i++)
    {
      double x2 = floor (x_values[i] * width);
      double y2 = height - floor (y_values[i] * height);
      double distance;

      if (x2 + NEAR_DISTANCE < x)
        continue;

      if (x2 > x + NEAR_DISTANCE)
        break;

      distance = sqrt (powf (x2 - x, 2) + powf (y2 - y, 2));

      if (distance < best_distance)
        {
          best_distance = distance;
          best_index = i;
          best_x = x2;
          best_y = y2;
        }
    }

  if (best_index != GTK_INVALID_LIST_POSITION)
    {
      GdkRGBA fill_color;
      const int size = 6;
      const int half_size = size / 2;
      graphene_rect_t area;
      cairo_t *cr;

      area = GRAPHENE_RECT_INIT (best_x - half_size, best_y - half_size, size, size);
      cr = gtk_snapshot_append_cairo (snapshot, &area);

      cairo_rectangle (cr, area.origin.x, area.origin.y, area.size.width, area.size.height);

      fill_color = *color;
      fill_color.alpha *= .75;
      gdk_cairo_set_source_rgba (cr, &fill_color);
      cairo_fill_preserve (cr);

      gdk_cairo_set_source_rgba (cr, color);
      cairo_set_line_width (cr, 1);
      cairo_stroke (cr);

      cairo_destroy (cr);
    }
}

static gpointer
sysprof_line_layer_lookup_item (SysprofChartLayer *layer,
                                double             x,
                                double             y)
{
  SysprofLineLayer *self = (SysprofLineLayer *)layer;
  SysprofXYSeries *series;
  const double *x_values;
  const double *y_values;
  GListModel *model;
  double best_distance = G_MAXDOUBLE;
  guint best_index = GTK_INVALID_LIST_POSITION;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_LINE_LAYER (self));

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  _sysprof_xy_layer_get_xy (SYSPROF_XY_LAYER (self), &x_values, &y_values, &n_values);

  if (width == 0 || height == 0 || n_values == 0)
    return NULL;

  series = sysprof_xy_layer_get_series (SYSPROF_XY_LAYER (self));
  model = sysprof_series_get_model (SYSPROF_SERIES (series));

  for (guint i = 0; i < n_values; i++)
    {
      double x2 = floor (x_values[i] * width);
      double y2 = height - floor (y_values[i] * height);
      double distance;

      if (x2 + NEAR_DISTANCE < x)
        continue;

      if (x2 > x + NEAR_DISTANCE)
        break;

      distance = sqrt (pow (x2 - x, 2) + pow (y2 - y, 2));

      if (distance < best_distance)
        {
          best_distance = distance;
          best_index = i;
        }
    }

  if (best_index != GTK_INVALID_LIST_POSITION)
    return g_list_model_get_item (model, best_index);

  return NULL;
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
    case PROP_ANTIALIAS:
      g_value_set_boolean (value, sysprof_line_layer_get_antialias (self));
      break;

    case PROP_COLOR:
      g_value_set_boxed (value, sysprof_line_layer_get_color (self));
      break;

    case PROP_DASHED:
      g_value_set_boolean (value, sysprof_line_layer_get_dashed (self));
      break;

    case PROP_FILL:
      g_value_set_boolean (value, sysprof_line_layer_get_fill (self));
      break;

    case PROP_SPLINE:
      g_value_set_boolean (value, sysprof_line_layer_get_spline (self));
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
    case PROP_ANTIALIAS:
      sysprof_line_layer_set_antialias (self, g_value_get_boolean (value));
      break;

    case PROP_COLOR:
      sysprof_line_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_DASHED:
      sysprof_line_layer_set_dashed (self, g_value_get_boolean (value));
      break;

    case PROP_FILL:
      sysprof_line_layer_set_fill (self, g_value_get_boolean (value));
      break;

    case PROP_SPLINE:
      sysprof_line_layer_set_spline (self, g_value_get_boolean (value));
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
  SysprofChartLayerClass *chart_layer_class = SYSPROF_CHART_LAYER_CLASS (klass);

  object_class->get_property = sysprof_line_layer_get_property;
  object_class->set_property = sysprof_line_layer_set_property;

  widget_class->snapshot = sysprof_line_layer_snapshot;

  chart_layer_class->snapshot_motion = sysprof_line_layer_snapshot_motion;
  chart_layer_class->lookup_item = sysprof_line_layer_lookup_item;

  properties [PROP_ANTIALIAS] =
    g_param_spec_boolean ("antialias", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_DASHED] =
    g_param_spec_boolean ("dashed", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILL] =
    g_param_spec_boolean ("fill", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SPLINE] =
    g_param_spec_boolean ("spline", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_line_layer_init (SysprofLineLayer *self)
{
  self->color.alpha = 1;
  self->antialias = TRUE;
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
      self->color_set = color != &black;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

gboolean
sysprof_line_layer_get_dashed (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->dashed;
}

void
sysprof_line_layer_set_dashed (SysprofLineLayer *self,
                             gboolean          dashed)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  dashed = !!dashed;

  if (dashed != self->dashed)
    {
      self->dashed = dashed;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DASHED]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
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
sysprof_line_layer_get_spline (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->spline;
}

void
sysprof_line_layer_set_spline (SysprofLineLayer *self,
                               gboolean          spline)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  spline = !!spline;

  if (spline != self->spline)
    {
      self->spline = spline;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPLINE]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

gboolean
sysprof_line_layer_get_antialias (SysprofLineLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_LINE_LAYER (self), FALSE);

  return self->antialias;
}

void
sysprof_line_layer_set_antialias (SysprofLineLayer *self,
                                  gboolean          antialias)
{
  g_return_if_fail (SYSPROF_IS_LINE_LAYER (self));

  antialias = !!antialias;

  if (antialias != self->antialias)
    {
      self->antialias = antialias;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANTIALIAS]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
