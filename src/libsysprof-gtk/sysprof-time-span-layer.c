/*
 * sysprof-time-span-layer.c
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

#include "sysprof-time-span-layer.h"

struct _SysprofTimeSpanLayer
{
  SysprofChartLayer parent_instance;

  SysprofTimeSeries *series;

  GdkRGBA color;
  GdkRGBA event_color;
};

G_DEFINE_FINAL_TYPE (SysprofTimeSpanLayer, sysprof_time_span_layer, SYSPROF_TYPE_CHART_LAYER)

enum {
  PROP_0,
  PROP_COLOR,
  PROP_EVENT_COLOR,
  PROP_SERIES,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

SysprofChartLayer *
sysprof_time_span_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_TIME_SPAN_LAYER, NULL);
}

static void
sysprof_time_span_layer_snapshot (GtkWidget   *widget,
                                  GtkSnapshot *snapshot)
{
  SysprofTimeSpanLayer *self = (SysprofTimeSpanLayer *)widget;
  const SysprofTimeSeriesValue *values;
  graphene_rect_t box_rect;
  float last_end_x = 0;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_TIME_SPAN_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (width == 0 || height == 0 || self->series == NULL || self->color.alpha == 0)
    return;

  if (!(values = sysprof_time_series_get_values (self->series, &n_values)))
    return;

  /* First pass, draw our rectangles for duration which we
   * always want in the background compared to "points" which
   * are marks w/o a duration.
   */
  for (guint i = 0; i < n_values; i++)
    {
      const SysprofTimeSeriesValue *v = &values[i];

      if (v->begin != v->end)
        {
          graphene_rect_t rect;
          float end_x;

          rect = GRAPHENE_RECT_INIT (floorf (v->begin * width),
                                     0,
                                     ceilf ((v->end - v->begin) * width),
                                     height);

          /* Ignore empty sized draws */
          if (rect.size.width == 0)
            continue;

          /* Cull draw unless it will extend past last rect */
          end_x = rect.origin.x + rect.size.width;
          if (end_x <= last_end_x)
            continue;
          else
            last_end_x = end_x;

          gtk_snapshot_append_color (snapshot, &self->color, &rect);
        }
    }

  box_rect = GRAPHENE_RECT_INIT (-ceil (height / 4.),
                                 -ceil (height / 4.),
                                 ceil (height/2.),
                                 ceil (height/2.));

  for (guint i = 0; i < n_values; i++)
    {
      const SysprofTimeSeriesValue *v = &values[i];

      if (v->begin == v->end)
        {
          gtk_snapshot_save (snapshot);
          gtk_snapshot_translate (snapshot,
                                  &GRAPHENE_POINT_INIT (v->begin * width, height / 2));
          gtk_snapshot_rotate (snapshot, 45.f);
          gtk_snapshot_append_color (snapshot, &self->event_color, &box_rect);
          gtk_snapshot_restore (snapshot);
        }
    }
}

static void
sysprof_time_span_layer_finalize (GObject *object)
{
  SysprofTimeSpanLayer *self = (SysprofTimeSpanLayer *)object;

  g_clear_pointer (&self->series, sysprof_time_series_unref);

  G_OBJECT_CLASS (sysprof_time_span_layer_parent_class)->finalize (object);
}

static void
sysprof_time_span_layer_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofTimeSpanLayer *self = SYSPROF_TIME_SPAN_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      g_value_set_boxed (value, sysprof_time_span_layer_get_color (self));
      break;

    case PROP_EVENT_COLOR:
      g_value_set_boxed (value, sysprof_time_span_layer_get_event_color (self));
      break;

    case PROP_SERIES:
      g_value_set_boxed (value, sysprof_time_span_layer_get_series (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_span_layer_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofTimeSpanLayer *self = SYSPROF_TIME_SPAN_LAYER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      sysprof_time_span_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_EVENT_COLOR:
      sysprof_time_span_layer_set_event_color (self, g_value_get_boxed (value));
      break;

    case PROP_SERIES:
      sysprof_time_span_layer_set_series (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_span_layer_class_init (SysprofTimeSpanLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_time_span_layer_finalize;
  object_class->get_property = sysprof_time_span_layer_get_property;
  object_class->set_property = sysprof_time_span_layer_set_property;

  widget_class->snapshot = sysprof_time_span_layer_snapshot;

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_EVENT_COLOR] =
    g_param_spec_boxed ("event-color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIES] =
    g_param_spec_boxed ("series", NULL, NULL,
                        SYSPROF_TYPE_TIME_SERIES,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_time_span_layer_init (SysprofTimeSpanLayer *self)
{
  self->color.alpha = 1;
  self->event_color.alpha = 1;
}

const GdkRGBA *
sysprof_time_span_layer_get_color (SysprofTimeSpanLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self), NULL);

  return &self->color;
}

void
sysprof_time_span_layer_set_color (SysprofTimeSpanLayer *self,
                                   const GdkRGBA        *color)
{
  static const GdkRGBA black = {0,0,0,1};

  g_return_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self));

  if (color == NULL)
    color = &black;

  if (!gdk_rgba_equal (&self->color, color))
    {
      self->color = *color;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

const GdkRGBA *
sysprof_time_span_layer_get_event_color (SysprofTimeSpanLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self), NULL);

  return &self->event_color;
}

void
sysprof_time_span_layer_set_event_color (SysprofTimeSpanLayer *self,
                                         const GdkRGBA        *event_color)
{
  static const GdkRGBA black = {0,0,0,1};

  g_return_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self));

  if (event_color == NULL)
    event_color = &black;

  if (!gdk_rgba_equal (&self->event_color, event_color))
    {
      self->event_color = *event_color;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EVENT_COLOR]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

/**
 * sysprof_time_span_layer_get_series:
 * @self: a #SysprofTimeSpanLayer
 *
 * Returns: (transfer none) (nullable): a #SysprofTimeSeries or %NULL
 */
SysprofTimeSeries *
sysprof_time_span_layer_get_series (SysprofTimeSpanLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self), NULL);

  return self->series;
}

void
sysprof_time_span_layer_set_series (SysprofTimeSpanLayer *self,
                                    SysprofTimeSeries    *series)
{
  g_return_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self));

  if (series == self->series)
    return;

  g_clear_pointer (&self->series, sysprof_time_series_unref);
  self->series = series ? sysprof_time_series_ref (series) : NULL;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}
