/* sysprof-column-layer.c
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

#include "sysprof-axis.h"
#include "sysprof-chart-layer-private.h"
#include "sysprof-column-layer.h"
#include "sysprof-normalized-series.h"
#include "sysprof-xy-layer-private.h"

struct _SysprofColumnLayer
{
  SysprofXYLayer parent_instance;

  GdkRGBA        color;
  GdkRGBA        hover_color;

  guint          color_set : 1;
  guint          hover_color_set : 1;
};

enum {
  PROP_0,
  PROP_COLOR,
  PROP_HOVER_COLOR,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofColumnLayer, sysprof_column_layer, SYSPROF_TYPE_XY_LAYER)

static GParamSpec *properties [N_PROPS];

static void
sysprof_column_layer_snapshot (GtkWidget   *widget,
                               GtkSnapshot *snapshot)
{
  SysprofColumnLayer *self = (SysprofColumnLayer *)widget;
  const double *x_values;
  const double *y_values;
  const GdkRGBA *color;
  double last_x = 0;
  double last_y = 0;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_COLUMN_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  _sysprof_xy_layer_get_xy (SYSPROF_XY_LAYER (self), &x_values, &y_values, &n_values);

  if (width == 0 || height == 0 || n_values == 0)
    return;

  if (self->color_set)
    color = &self->color;
  else
    color = _sysprof_chart_layer_get_accent_bg_color ();

  if (color->alpha == .0)
    return;

  gtk_snapshot_save (snapshot);

  if (!sysprof_xy_layer_get_flip_y (SYSPROF_XY_LAYER (self)))
    {
      graphene_matrix_t flip_y;

      graphene_matrix_init_from_2d (&flip_y, 1, 0, 0, -1, 0, height);
      gtk_snapshot_transform_matrix (snapshot, &flip_y);
    }

  for (guint i = 0; i < n_values; i++)
    {
      double x;
      double y;

      if (x_values[i] < .0)
        continue;

      if (x_values[i] > 1.)
        break;

      x = round (x_values[i] * width);
      y = ceil (y_values[i] * height);

      if (x == last_x && y < last_y)
        continue;

      gtk_snapshot_append_color (snapshot,
                                 color,
                                 &GRAPHENE_RECT_INIT (x, 0, 1, y));

      last_x = x;
      last_y = y;
    }

  gtk_snapshot_restore (snapshot);
}

static guint
sysprof_column_layer_get_index_at_coord (SysprofColumnLayer *self,
                                         double              x,
                                         double              y,
                                         graphene_rect_t    *area)
{
  graphene_point_t point;
  const double *x_values;
  const double *y_values;
  gboolean flip_y;
  guint best = GTK_INVALID_LIST_POSITION;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_COLUMN_LAYER (self));

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));
  flip_y = sysprof_xy_layer_get_flip_y (SYSPROF_XY_LAYER (self));

  _sysprof_xy_layer_get_xy (SYSPROF_XY_LAYER (self), &x_values, &y_values, &n_values);

  if (width == 0 || height == 0 || n_values == 0)
    return GTK_INVALID_LIST_POSITION;

  point = GRAPHENE_POINT_INIT (x, y);

  for (guint i = 0; i < n_values; i++)
    {
      graphene_rect_t rect;

      if (flip_y)
        rect = GRAPHENE_RECT_INIT (x_values[i]*width,
                                   0,
                                   1,
                                   y_values[i]*height);
      else
        rect = GRAPHENE_RECT_INIT (x_values[i]*width,
                                   height - y_values[i]*height,
                                   1,
                                   y_values[i]*height);

      if (graphene_rect_contains_point (&rect, &point))
        {
          if (area != NULL)
            *area = rect;

          return i;
        }

      if (rect.origin.x <= x &&
          rect.origin.x + rect.size.width >= x)
        {
          if (area != NULL)
            *area = rect;

          best = i;
        }
    }

  return best;
}

static void
sysprof_column_layer_snapshot_motion (SysprofChartLayer *layer,
                                      GtkSnapshot       *snapshot,
                                      double             x,
                                      double             y)
{
  SysprofColumnLayer *self = (SysprofColumnLayer *)layer;
  graphene_rect_t rect;
  guint position;

  g_assert (SYSPROF_IS_COLUMN_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  position = sysprof_column_layer_get_index_at_coord (self, x, y, &rect);

  if (position != GTK_INVALID_LIST_POSITION)
    gtk_snapshot_append_color (snapshot, &self->hover_color, &rect);
}

static gpointer
sysprof_column_layer_lookup_item (SysprofChartLayer *layer,
                                  double             x,
                                  double             y)
{
  SysprofColumnLayer *self = (SysprofColumnLayer *)layer;
  SysprofXYSeries *series;
  GListModel *model;

  g_assert (SYSPROF_IS_COLUMN_LAYER (self));

  if ((series = sysprof_xy_layer_get_series (SYSPROF_XY_LAYER (layer))) &&
      (model = sysprof_series_get_model (SYSPROF_SERIES (series))))
    {
      guint position = sysprof_column_layer_get_index_at_coord (self, x, y, NULL);

      if (position != GTK_INVALID_LIST_POSITION)
        return g_list_model_get_item (model, position);
    }

  return NULL;
}

static void
sysprof_column_layer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofColumnLayer *self = SYSPROF_COLUMN_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, &self->color);
      break;

    case PROP_HOVER_COLOR:
      g_value_set_boxed (value, &self->hover_color);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_column_layer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofColumnLayer *self = SYSPROF_COLUMN_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      sysprof_column_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_HOVER_COLOR:
      sysprof_column_layer_set_hover_color (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_column_layer_class_init (SysprofColumnLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofChartLayerClass *chart_layer_class = SYSPROF_CHART_LAYER_CLASS (klass);

  object_class->get_property = sysprof_column_layer_get_property;
  object_class->set_property = sysprof_column_layer_set_property;

  widget_class->snapshot = sysprof_column_layer_snapshot;

  chart_layer_class->lookup_item = sysprof_column_layer_lookup_item;
  chart_layer_class->snapshot_motion = sysprof_column_layer_snapshot_motion;

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_HOVER_COLOR] =
    g_param_spec_boxed ("hover-color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_column_layer_init (SysprofColumnLayer *self)
{
  self->color = (GdkRGBA) {0,0,0,1};
  self->hover_color = (GdkRGBA) {1,0,0,1};
}

SysprofChartLayer *
sysprof_column_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_COLUMN_LAYER, NULL);
}

const GdkRGBA *
sysprof_column_layer_get_color (SysprofColumnLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_COLUMN_LAYER (self), NULL);

  return &self->color;
}

void
sysprof_column_layer_set_color (SysprofColumnLayer *self,
                                const GdkRGBA      *color)
{
  static const GdkRGBA black = {0,0,0,1};

  g_return_if_fail (SYSPROF_IS_COLUMN_LAYER (self));

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

const GdkRGBA *
sysprof_column_layer_get_hover_color (SysprofColumnLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_COLUMN_LAYER (self), NULL);

  return &self->hover_color;
}

void
sysprof_column_layer_set_hover_color (SysprofColumnLayer *self,
                                      const GdkRGBA      *hover_color)
{
  static const GdkRGBA red = {1,0,0,1};

  g_return_if_fail (SYSPROF_IS_COLUMN_LAYER (self));

  if (hover_color == NULL)
    hover_color = &red;

  if (!gdk_rgba_equal (&self->hover_color, hover_color))
    {
      self->hover_color = *hover_color;
      self->hover_color_set = hover_color != &red;

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVER_COLOR]);

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
