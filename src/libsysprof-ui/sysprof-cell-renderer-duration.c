/* sysprof-cell-renderer-duration.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-cell-renderer-duration"

#include "config.h"

#include "sysprof-cell-renderer-duration.h"

typedef struct
{
  gint64 begin_time;
  gint64 end_time;
  gint64 zoom_begin;
  gint64 zoom_end;
} SysprofCellRendererDurationPrivate;

enum {
  PROP_0,
  PROP_BEGIN_TIME,
  PROP_END_TIME,
  PROP_ZOOM_BEGIN,
  PROP_ZOOM_END,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofCellRendererDuration, sysprof_cell_renderer_duration, GTK_TYPE_CELL_RENDERER)

static GParamSpec *properties [N_PROPS];

static void
sysprof_cell_renderer_duration_render (GtkCellRenderer      *renderer,
                                       cairo_t              *cr,
                                       GtkWidget            *widget,
                                       const GdkRectangle   *bg_area,
                                       const GdkRectangle   *cell_area,
                                       GtkCellRendererState  state)
{
  SysprofCellRendererDuration *self = (SysprofCellRendererDuration *)renderer;
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);
  GtkStyleContext *style_context;
  gdouble zoom_range;
  gdouble x1, x2;
  GdkRGBA rgba;
  GdkRectangle r;

  g_assert (SYSPROF_IS_CELL_RENDERER_DURATION (self));
  g_assert (cr != NULL);
  g_assert (GTK_IS_WIDGET (widget));

  if (priv->end_time >= priv->begin_time)
    {
      if (priv->begin_time > priv->zoom_end || priv->end_time < priv->zoom_begin)
        return;
    }

  style_context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_color (style_context,
                               gtk_style_context_get_state (style_context),
                               &rgba);

  zoom_range = (gdouble)priv->zoom_end - (gdouble)priv->zoom_begin;

  x1 = (priv->begin_time - priv->zoom_begin) / zoom_range * cell_area->width;
  x2 = (priv->end_time - priv->zoom_begin) / zoom_range * cell_area->width;

  if (x2 < x1)
    x2 = x1;

  r.x = cell_area->x + x1;
  r.y = cell_area->y;
  r.width = MAX (1.0, x2 - x1);
  r.height = cell_area->height;

  gdk_cairo_rectangle (cr, &r);
  gdk_cairo_set_source_rgba (cr, &rgba);
  cairo_fill (cr);
}

static void
sysprof_cell_renderer_duration_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  SysprofCellRendererDuration *self = SYSPROF_CELL_RENDERER_DURATION (object);
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_BEGIN_TIME:
      g_value_set_int64 (value, priv->begin_time);
      break;

    case PROP_ZOOM_BEGIN:
      g_value_set_int64 (value, priv->zoom_begin);
      break;

    case PROP_ZOOM_END:
      g_value_set_int64 (value, priv->zoom_end);
      break;

    case PROP_END_TIME:
      g_value_set_int64 (value, priv->end_time);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cell_renderer_duration_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  SysprofCellRendererDuration *self = SYSPROF_CELL_RENDERER_DURATION (object);
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_BEGIN_TIME:
      priv->begin_time = g_value_get_int64 (value);
      break;

    case PROP_ZOOM_BEGIN:
      priv->zoom_begin = g_value_get_int64 (value);
      break;

    case PROP_ZOOM_END:
      priv->zoom_end = g_value_get_int64 (value);
      break;

    case PROP_END_TIME:
      priv->end_time = g_value_get_int64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cell_renderer_duration_class_init (SysprofCellRendererDurationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (klass);

  object_class->get_property = sysprof_cell_renderer_duration_get_property;
  object_class->set_property = sysprof_cell_renderer_duration_set_property;

  cell_class->render = sysprof_cell_renderer_duration_render;

  properties [PROP_BEGIN_TIME] =
    g_param_spec_int64 ("begin-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_BEGIN] =
    g_param_spec_int64 ("zoom-begin", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_END] =
    g_param_spec_int64 ("zoom-end", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_cell_renderer_duration_init (SysprofCellRendererDuration *self)
{
}

GtkCellRenderer *
sysprof_cell_renderer_duration_new (void)
{
  return g_object_new (SYSPROF_TYPE_CELL_RENDERER_DURATION, NULL);
}
