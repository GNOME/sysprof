/* sysprof-depth-layer.c
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

#include "sysprof-depth-layer.h"

struct _SysprofDepthLayer
{
  SysprofChartLayer parent_instance;
  SysprofXYSeries *series;
  GdkRGBA color;
};

enum {
  PROP_0,
  PROP_COLOR,
  PROP_SERIES,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDepthLayer, sysprof_depth_layer, SYSPROF_TYPE_CHART_LAYER)

static GParamSpec *properties [N_PROPS];

static void
sysprof_depth_layer_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  SysprofDepthLayer *self = (SysprofDepthLayer *)widget;
  const SysprofXYSeriesValue *values;
  guint n_values;
  double min_x, max_x;
  int line_width;
  int width;
  int height;

  g_assert (SYSPROF_IS_DEPTH_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->series == NULL || self->color.alpha == 0)
    return;

  if (!(values = sysprof_xy_series_get_values (self->series, &n_values)))
    return;

  sysprof_xy_series_get_range (self->series, &min_x, NULL, &max_x, NULL);

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_width (widget);

  line_width = MAX (1, (max_x - min_x) / width);

  for (guint i = 0; i < n_values; i++)
    {
      const SysprofXYSeriesValue *v = &values[i];

      gtk_snapshot_append_color (snapshot,
                                 &self->color,
                                 &GRAPHENE_RECT_INIT (v->x * width,
                                                      height,
                                                      line_width,
                                                      height - (v->y * height)));
    }
}

static void
sysprof_depth_layer_dispose (GObject *object)
{
  SysprofDepthLayer *self = (SysprofDepthLayer *)object;

  g_clear_pointer (&self->series, sysprof_xy_series_unref);

  G_OBJECT_CLASS (sysprof_depth_layer_parent_class)->dispose (object);
}

static void
sysprof_depth_layer_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofDepthLayer *self = SYSPROF_DEPTH_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, &self->color);
      break;

    case PROP_SERIES:
      g_value_set_boxed (value, self->series);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_depth_layer_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofDepthLayer *self = SYSPROF_DEPTH_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      sysprof_depth_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_SERIES:
      sysprof_depth_layer_set_series (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_depth_layer_class_init (SysprofDepthLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_depth_layer_dispose;
  object_class->get_property = sysprof_depth_layer_get_property;
  object_class->set_property = sysprof_depth_layer_set_property;

  widget_class->snapshot = sysprof_depth_layer_snapshot;

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                         GDK_TYPE_RGBA,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIES] =
    g_param_spec_boxed ("series", NULL, NULL,
                         SYSPROF_TYPE_XY_SERIES,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_depth_layer_init (SysprofDepthLayer *self)
{
  self->color.alpha = 1;
}

SysprofChartLayer *
sysprof_depth_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_DEPTH_LAYER, NULL);
}

const GdkRGBA *
sysprof_depth_layer_get_color (SysprofDepthLayer *self)
{
  return &self->color;
}

void
sysprof_depth_layer_set_color (SysprofDepthLayer *self,
                               const GdkRGBA     *color)
{
  static const GdkRGBA black = {0,0,0,1};

  g_return_if_fail (SYSPROF_IS_DEPTH_LAYER (self));

  if (color == NULL)
    color = &black;

  if (!gdk_rgba_equal (&self->color, color))
    {
      self->color = *color;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    }
}

/**
 * sysprof_depth_layer_get_series:
 * @self: a #SysprofDepthLayer
 *
 * Gets the data series to be drawn.
 *
 * Returns: (transfer none) (nullable): a #SysprofXYSeries or %NULL
 */
SysprofXYSeries *
sysprof_depth_layer_get_series (SysprofDepthLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_DEPTH_LAYER (self), NULL);

  return self->series;
}

void
sysprof_depth_layer_set_series (SysprofDepthLayer *self,
                                SysprofXYSeries   *series)
{
  g_return_if_fail (SYSPROF_IS_DEPTH_LAYER (self));

  if (series == self->series)
    return;

  g_clear_pointer (&self->series, sysprof_xy_series_unref);

  if (series)
    self->series = sysprof_xy_series_ref (series);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}
