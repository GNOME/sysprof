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

#include <adwaita.h>

#include "sysprof-chart-layer-private.h"
#include "sysprof-color-iter-private.h"
#include "sysprof-normalized-series.h"
#include "sysprof-time-series-item.h"
#include "sysprof-time-span-layer.h"

struct _SysprofTimeSpanLayer
{
  SysprofChartLayer parent_instance;

  SysprofAxis *axis;
  SysprofTimeSeries *series;

  SysprofNormalizedSeries *normal_x;
  SysprofNormalizedSeries *normal_x2;

  GBindingGroup *series_bindings;

  GHashTable *colors_hash;

  SysprofColorIter colors;

  GdkRGBA color;
  GdkRGBA event_color;

  guint auto_color : 1;
  guint color_set : 1;
  guint event_color_set : 1;
};

G_DEFINE_FINAL_TYPE (SysprofTimeSpanLayer, sysprof_time_span_layer, SYSPROF_TYPE_CHART_LAYER)

enum {
  PROP_0,
  PROP_AUTO_COLOR,
  PROP_AXIS,
  PROP_COLOR,
  PROP_EVENT_COLOR,
  PROP_NORMALIZED_X,
  PROP_NORMALIZED_X2,
  PROP_SERIES,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

SysprofChartLayer *
sysprof_time_span_layer_new (void)
{
  return g_object_new (SYSPROF_TYPE_TIME_SPAN_LAYER, NULL);
}

static inline void
premix_colors (GdkRGBA       *dest,
               const GdkRGBA *fg,
               const GdkRGBA *bg,
               gboolean       bg_set,
               double         alpha)
{
	g_assert (dest != NULL);
	g_assert (fg != NULL);
	g_assert (bg != NULL || bg_set == FALSE);
	g_assert (alpha >= 0.0 && alpha <= 1.0);

  if (bg_set)
    {
      dest->red = ((1 - alpha) * bg->red) + (alpha * fg->red);
      dest->green = ((1 - alpha) * bg->green) + (alpha * fg->green);
      dest->blue = ((1 - alpha) * bg->blue) + (alpha * fg->blue);
      dest->alpha = 1.0;
    }
  else
    {
      *dest = *fg;
      dest->alpha = alpha;
    }
}

static void
sysprof_time_span_layer_snapshot (GtkWidget   *widget,
                                  GtkSnapshot *snapshot)
{
  SysprofTimeSpanLayer *self = (SysprofTimeSpanLayer *)widget;
  const GdkRGBA *label_color = _sysprof_chart_layer_get_accent_fg_color ();
  static const GdkRGBA black = {0,0,0,1};
  g_autoptr(PangoLayout) layout = NULL;
  const double *x_values;
  const double *x2_values;
  const GdkRGBA *color;
  const GdkRGBA *event_color;
  GdkRGBA mixed;
  gboolean dark;
  graphene_rect_t box_rect;
  guint n_x_values = 0;
  guint n_x2_values = 0;
  guint n_values;
  int layout_y = 0;
  int layout_h;
  int width;
  int height;

  g_assert (SYSPROF_IS_TIME_SPAN_LAYER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (self->color_set)
    color = &self->color;
  else
    color = _sysprof_chart_layer_get_accent_bg_color ();

  if (self->event_color_set)
    event_color = &self->event_color;
  else
    {
      premix_colors (&mixed,
                     _sysprof_chart_layer_get_accent_bg_color (),
                     _sysprof_chart_layer_get_accent_fg_color (),
                     TRUE, .5);
      event_color = &mixed;
    }

  if (width == 0 || height == 0)
    return;

  if (!(x_values = sysprof_normalized_series_get_values (self->normal_x, &n_x_values)) ||
      !(x2_values = sysprof_normalized_series_get_values (self->normal_x2, &n_x2_values)) ||
      !(n_values = MIN (n_x_values, n_x2_values)))
    return;

  if (color->alpha > 0 || event_color->alpha > 0)
    {
      layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), NULL);

      pango_layout_set_single_paragraph_mode (layout, TRUE);
      pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_text (layout, "XM0", -1);
      pango_layout_get_pixel_size (layout, NULL, &layout_h);

      layout_y = (height - layout_h) / 2;
    }

  if (color->alpha > 0)
    {
      /* First pass, draw our rectangles for duration which we
       * always want in the background compared to "points" which
       * are marks w/o a duration.
       */
      for (guint i = 0; i < n_values; i++)
        {
          double begin = x_values[i];
          double end = x2_values[i];

          if (end < .0)
            continue;

          if (begin > 1.)
            break;

          if (begin != end)
            {
              g_autofree char *label = sysprof_time_series_dup_label (self->series, i);
              graphene_rect_t rect;

              rect = GRAPHENE_RECT_INIT (floor (begin * width),
                                         0,
                                         ceil ((end - begin) * width),
                                         height);

              /* Ignore empty draws */
              if (rect.size.width == 0)
                continue;

              if (rect.origin.x < 0)
                {
                  rect.size.width -= ABS (rect.origin.x);
                  rect.origin.x = 0;
                }

              if (rect.origin.x + rect.size.width > width)
                rect.size.width = width - rect.origin.x;

              if (self->auto_color && label)
                {
                  const GdkRGBA *this_color = g_hash_table_lookup (self->colors_hash, label);

                  if (this_color == NULL)
                    {
                      const GdkRGBA *next_color = sysprof_color_iter_next (&self->colors);
                      g_hash_table_insert (self->colors_hash, g_strdup (label), (GdkRGBA *)next_color);
                      this_color = next_color;
                    }

                  gtk_snapshot_append_color (snapshot, this_color, &rect);
                }
              else
                {
                  gtk_snapshot_append_color (snapshot, color, &rect);
                }

              if (label != NULL)
                {
                  gboolean rect_fits_text;
                  int right_empty_space;
                  int label_width;

                  pango_layout_set_text (layout, label, -1);
                  pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);

                  pango_layout_get_pixel_size (layout, &label_width, NULL);

                  /* If it fits 75% of the text inside the rect, it's enough and
                   * won't look too busy.
                   */
                  rect_fits_text = ((rect.size.width - 6) / (float) label_width) > 0.75;
                  right_empty_space = width - (rect.origin.x + rect.size.width) - 6;

                  /* Draw inside the rect if the label fits, or if there's not more
                   * space outside the rect.
                   */
                  if (rect_fits_text || (rect.size.width - 6) > right_empty_space)
                    {
                      pango_layout_set_width (layout, (rect.size.width - 6) * PANGO_SCALE);

                      gtk_snapshot_save (snapshot);
                      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (rect.origin.x + 3, layout_y));
                      gtk_snapshot_append_layout (snapshot, layout, label_color);
                      gtk_snapshot_restore (snapshot);
                    }
                  else if (n_values == 1 && right_empty_space > 20)
                    {
                      const GdkRGBA *fg = dark ? label_color : &black;
                      pango_layout_set_width (layout, right_empty_space * PANGO_SCALE);

                      gtk_snapshot_save (snapshot);
                      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (rect.origin.x + rect.size.width + 3, layout_y));
                      gtk_snapshot_append_layout (snapshot, layout, fg);
                      gtk_snapshot_restore (snapshot);
                    }
                }
            }
        }
    }

  if (event_color->alpha > 0)
    {
      double last_x = -1;

      box_rect = GRAPHENE_RECT_INIT (-ceil (height / 6.),
                                     -ceil (height / 6.),
                                     ceil (height / 3.),
                                     ceil (height / 3.));

      for (guint i = 0; i < n_values; i++)
        {
          double begin = x_values[i];
          double end = x2_values[i];

          if (begin != end)
            continue;

          if (last_x != begin)
            {
              gtk_snapshot_save (snapshot);
              gtk_snapshot_translate (snapshot,
                                      &GRAPHENE_POINT_INIT (begin * width, height / 2));
              gtk_snapshot_rotate (snapshot, 45.f);
              gtk_snapshot_append_color (snapshot, event_color, &box_rect);
              gtk_snapshot_restore (snapshot);

              last_x = begin;

              if (n_values == 1)
                {
                  g_autofree char *label = sysprof_time_series_dup_label (self->series, i);
                  const GdkRGBA *fg = dark ? label_color : &black;
                  int right_empty_space;
                  int label_width;
                  int label_x;

                  if (label == NULL || *label == '\0')
                    continue;

                  pango_layout_set_text (layout, label, -1);
                  pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);
                  pango_layout_get_pixel_size (layout, &label_width, NULL);

                  label_x = begin * width + ceil (height / 3.) + 2;
                  right_empty_space = width - label_x;
                  pango_layout_set_width (layout, right_empty_space * PANGO_SCALE);

                  if (right_empty_space <= 20)
                    continue;

                  gtk_snapshot_save (snapshot);
                  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (label_x, layout_y));
                  gtk_snapshot_append_layout (snapshot, layout, fg);
                  gtk_snapshot_restore (snapshot);
                }
            }
        }
    }
}

static gpointer
sysprof_time_span_layer_lookup_item (SysprofChartLayer *layer,
                                     double             x,
                                     double             y)
{
  SysprofTimeSpanLayer *self = (SysprofTimeSpanLayer *)layer;
  const double *x_values;
  const double *x2_values;
  GListModel *model;
  guint n_x_values = 0;
  guint n_x2_values = 0;
  guint n_values;
  int width;
  int height;

  g_assert (SYSPROF_IS_TIME_SPAN_LAYER (self));

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  if (width == 0 || height == 0 || self->series == NULL)
    return NULL;

  if (!(model = sysprof_series_get_model (SYSPROF_SERIES (self->series))))
    return NULL;

  if (!(x_values = sysprof_normalized_series_get_values (self->normal_x, &n_x_values)) ||
      !(x2_values = sysprof_normalized_series_get_values (self->normal_x2, &n_x2_values)) ||
      !(n_values = MIN (n_x_values, n_x2_values)))
    return NULL;

  /* First match our non-duration marks (diamonds) */
  for (guint i = 0; i < n_values; i++)
    {
      int begin = x_values[i] * width - 3;
      int end = x2_values[i] * width + 3;

      if (x_values[i] != x2_values[i])
        continue;

      if (x >= begin && x < end)
        return g_list_model_get_item (model, i);
    }

  /* Then match regular duration events */
  for (guint i = 0; i < n_values; i++)
    {
      double begin = x_values[i] * width;
      double end = x2_values[i] * width;

      if (x_values[i] == x2_values[i])
        continue;

      if (x >= begin && x < end)
        return g_list_model_get_item (model, i);
    }

  return NULL;
}

static void
sysprof_time_span_layer_finalize (GObject *object)
{
  SysprofTimeSpanLayer *self = (SysprofTimeSpanLayer *)object;

  g_clear_object (&self->series_bindings);
  g_clear_object (&self->axis);
  g_clear_object (&self->series);
  g_clear_object (&self->normal_x);
  g_clear_object (&self->normal_x2);
  g_clear_pointer (&self->colors_hash, g_hash_table_unref);

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
    case PROP_AUTO_COLOR:
      g_value_set_boolean (value, self->auto_color);
      break;

    case PROP_AXIS:
      g_value_set_object (value, sysprof_time_span_layer_get_axis (self));
      break;

    case PROP_COLOR:
      g_value_set_boxed (value, sysprof_time_span_layer_get_color (self));
      break;

    case PROP_EVENT_COLOR:
      g_value_set_boxed (value, sysprof_time_span_layer_get_event_color (self));
      break;

    case PROP_NORMALIZED_X:
      g_value_set_object (value, self->normal_x);
      break;

    case PROP_NORMALIZED_X2:
      g_value_set_object (value, self->normal_x2);
      break;

    case PROP_SERIES:
      g_value_set_object (value, sysprof_time_span_layer_get_series (self));
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
    case PROP_AUTO_COLOR:
      self->auto_color = g_value_get_boolean (value);
      break;

    case PROP_AXIS:
      sysprof_time_span_layer_set_axis (self, g_value_get_object (value));
      break;

    case PROP_COLOR:
      sysprof_time_span_layer_set_color (self, g_value_get_boxed (value));
      break;

    case PROP_EVENT_COLOR:
      sysprof_time_span_layer_set_event_color (self, g_value_get_boxed (value));
      break;

    case PROP_SERIES:
      sysprof_time_span_layer_set_series (self, g_value_get_object (value));
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
  SysprofChartLayerClass *chart_layer_class = SYSPROF_CHART_LAYER_CLASS (klass);

  object_class->finalize = sysprof_time_span_layer_finalize;
  object_class->get_property = sysprof_time_span_layer_get_property;
  object_class->set_property = sysprof_time_span_layer_set_property;

  widget_class->snapshot = sysprof_time_span_layer_snapshot;

  chart_layer_class->lookup_item = sysprof_time_span_layer_lookup_item;

  properties[PROP_AUTO_COLOR] =
    g_param_spec_boolean ("auto-color", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_AXIS] =
    g_param_spec_object ("axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_EVENT_COLOR] =
    g_param_spec_boxed ("event-color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_NORMALIZED_X] =
    g_param_spec_object ("normalized-x", NULL, NULL,
                         SYSPROF_TYPE_NORMALIZED_SERIES,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_NORMALIZED_X2] =
    g_param_spec_object ("normalized-x2", NULL, NULL,
                         SYSPROF_TYPE_NORMALIZED_SERIES,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIES] =
    g_param_spec_object ("series", NULL, NULL,
                         SYSPROF_TYPE_TIME_SERIES,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "timespanlayer");
}

static void
sysprof_time_span_layer_init (SysprofTimeSpanLayer *self)
{
  self->normal_x = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES, NULL);
  self->normal_x2 = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES, NULL);

  self->colors_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  sysprof_color_iter_init (&self->colors);
  self->auto_color = TRUE;

  g_signal_connect_object (self->normal_x,
                           "items-changed",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->normal_x2,
                           "items-changed",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);

  self->series_bindings = g_binding_group_new ();
  g_binding_group_bind (self->series_bindings, "begin-time-expression",
                        self->normal_x, "expression",
                        G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->series_bindings, "end-time-expression",
                        self->normal_x2, "expression",
                        G_BINDING_SYNC_CREATE);
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
      self->color_set = color != &black;
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
      self->color_set = event_color != &black;
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

  if (!g_set_object (&self->series, series))
    return;

  g_binding_group_set_source (self->series_bindings, series);

  sysprof_normalized_series_set_series (self->normal_x, SYSPROF_SERIES (series));
  sysprof_normalized_series_set_series (self->normal_x2, SYSPROF_SERIES (series));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);
}

/**
 * sysprof_time_span_layer_get_axis:
 * @self: a #SysprofTimeSpanLayer
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis or %NULL
 */
SysprofAxis *
sysprof_time_span_layer_get_axis (SysprofTimeSpanLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self), NULL);

  return self->axis;
}

void
sysprof_time_span_layer_set_axis (SysprofTimeSpanLayer *self,
                                  SysprofAxis          *axis)
{
  g_return_if_fail (SYSPROF_IS_TIME_SPAN_LAYER (self));
  g_return_if_fail (!axis || SYSPROF_IS_AXIS (axis));

  if (!g_set_object (&self->axis, axis))
    return;

  sysprof_normalized_series_set_axis (self->normal_x, axis);
  sysprof_normalized_series_set_axis (self->normal_x2, axis);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AXIS]);
}
