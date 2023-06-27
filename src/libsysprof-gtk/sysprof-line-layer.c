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
#include "sysprof-xy-layer-private.h"

struct _SysprofLineLayer
{
  SysprofXYLayer parent_instance;

  GdkRGBA color;

  guint dashed : 1;
  guint fill : 1;
  guint flip_y : 1;
  guint spline : 1;
};

struct _SysprofLineLayerClass
{
  SysprofXYLayerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofLineLayer, sysprof_line_layer, SYSPROF_TYPE_XY_LAYER)

enum {
  PROP_0,
  PROP_COLOR,
  PROP_DASHED,
  PROP_FILL,
  PROP_FLIP_Y,
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
  const float *x_values;
  const float *y_values;
  cairo_t *cr;
  float first_x;
  float first_y;
  float last_x;
  float last_y;
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

  cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));

  cairo_set_line_width (cr, 1);

  if (!self->flip_y)
    cairo_set_matrix (cr, &(cairo_matrix_t) {1, 0, 0, -1, 0, height});

  first_x = last_x = floor (x_values[0] * width);
  first_y = last_y = floor (y_values[0] * height);

  cairo_move_to (cr, first_x, first_y);

  if (self->spline)
    {
      for (guint i = 1; i < n_values; i++)
        {
          float x = floor (x_values[i] * width);
          float y = floor (y_values[i] * height);

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
          float x = floor (x_values[i] * width);
          float y = floor (y_values[i] * height);

          cairo_line_to (cr, x, y);

          last_x = x;
          last_y = y;
        }
    }

  if (self->dashed)
    cairo_set_dash (cr, (double[]){2}, 1, 0);
  gdk_cairo_set_source_rgba (cr, &self->color);
  cairo_stroke_preserve (cr);

  if (self->fill)
    {
      GdkRGBA fill_color = self->color;

      fill_color.alpha *= .25;
      gdk_cairo_set_source_rgba (cr, &fill_color);

      cairo_line_to (cr, last_x, 0);
      cairo_line_to (cr, first_x, 0);
      cairo_line_to (cr, first_x, first_y);
      cairo_fill (cr);
    }

  cairo_destroy (cr);
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

    case PROP_DASHED:
      g_value_set_boolean (value, sysprof_line_layer_get_dashed (self));
      break;

    case PROP_FILL:
      g_value_set_boolean (value, sysprof_line_layer_get_fill (self));
      break;

    case PROP_FLIP_Y:
      g_value_set_boolean (value, sysprof_line_layer_get_flip_y (self));
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
    case PROP_COLOR:
      sysprof_line_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_DASHED:
      sysprof_line_layer_set_dashed (self, g_value_get_boolean (value));
      break;

    case PROP_FILL:
      sysprof_line_layer_set_fill (self, g_value_get_boolean (value));
      break;

    case PROP_FLIP_Y:
      sysprof_line_layer_set_flip_y (self, g_value_get_boolean (value));
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

  object_class->get_property = sysprof_line_layer_get_property;
  object_class->set_property = sysprof_line_layer_set_property;

  widget_class->snapshot = sysprof_line_layer_snapshot;

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

  properties [PROP_FLIP_Y] =
    g_param_spec_boolean ("flip-y", NULL, NULL,
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
