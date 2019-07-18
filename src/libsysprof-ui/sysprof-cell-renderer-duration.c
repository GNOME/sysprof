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
#include "sysprof-ui-private.h"
#include "sysprof-zoom-manager.h"

typedef struct
{
  gint64 capture_begin_time;
  gint64 capture_end_time;
  gint64 capture_duration;
  gint64 begin_time;
  gint64 end_time;
  gchar *text;
  SysprofZoomManager *zoom_manager;
  GdkRGBA color;
  guint color_set : 1;
} SysprofCellRendererDurationPrivate;

enum {
  PROP_0,
  PROP_BEGIN_TIME,
  PROP_CAPTURE_BEGIN_TIME,
  PROP_CAPTURE_END_TIME,
  PROP_COLOR,
  PROP_END_TIME,
  PROP_TEXT,
  PROP_ZOOM_MANAGER,
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
  g_autoptr(GString) str = NULL;
  GtkStyleContext *style_context;
  gdouble x1, x2;
  GdkRGBA rgba;
  GdkRectangle r;
  gint64 duration;

  g_assert (SYSPROF_IS_CELL_RENDERER_DURATION (self));
  g_assert (cr != NULL);
  g_assert (GTK_IS_WIDGET (widget));

  if (priv->zoom_manager == NULL)
    return;

  style_context = gtk_widget_get_style_context (widget);

  if (priv->color_set)
    rgba = priv->color;
  else
    gtk_style_context_get_color (style_context,
                                 gtk_style_context_get_state (style_context),
                                 &rgba);

  duration = sysprof_zoom_manager_get_duration_for_width (priv->zoom_manager, bg_area->width);

  x1 = (priv->begin_time - priv->capture_begin_time) / (gdouble)duration * cell_area->width;
  x2 = (priv->end_time - priv->capture_begin_time) / (gdouble)duration * cell_area->width;

  if (x2 < x1)
    x2 = x1;

  r.x = cell_area->x + x1;
  r.height = 12;
  r.y = cell_area->y + (cell_area->height - r.height) / 2;
  r.width = MAX (1.0, x2 - x1);

  if ((cell_area->height - r.height) % 2 == 1)
    r.height++;

  gdk_cairo_set_source_rgba (cr, &rgba);

  if (r.width > 3)
    {
      dzl_cairo_rounded_rectangle (cr, &r, 2, 2);
      cairo_fill (cr);
    }
  else if (r.width > 1)
    {
      gdk_cairo_rectangle (cr, &r);
      cairo_fill (cr);
    }
  else
    {
      cairo_set_line_width (cr, 1);
      cairo_move_to (cr, r.x + .5, r.y);
      cairo_line_to (cr, r.x + .5, r.y + r.height);
      cairo_stroke (cr);
    }

  str = g_string_new (NULL);

  if (priv->begin_time != priv->end_time)
    {
      g_autofree gchar *fmt = _sysprof_format_duration (priv->end_time - priv->begin_time);
      g_string_append_printf (str, "%s — ", fmt);
    }

  if (priv->text != NULL)
    g_string_append (str, priv->text);

  if (str->len)
    {
      PangoLayout *layout;
      gint w, h;

      /* Add some spacing before/after */
      r.x -= 24;
      r.width += 48;

      layout = gtk_widget_create_pango_layout (widget, NULL);
      pango_layout_set_text (layout, str->str, str->len);
      pango_layout_get_pixel_size (layout, &w, &h);

      if ((r.x + r.width + w) < (cell_area->x + cell_area->width) ||
          ((cell_area->x + w) > r.x))
        cairo_move_to (cr, r.x + r.width, r.y + ((r.height - h) / 2));
      else
        cairo_move_to (cr, r.x - w, r.y + ((r.height - h) / 2));

      if (priv->end_time < priv->begin_time)
        {
          gdk_rgba_parse (&rgba, "#f00");
          if (state & GTK_CELL_RENDERER_SELECTED)
            rgba.alpha = 0.6;
        }

      gdk_cairo_set_source_rgba (cr, &rgba);
      pango_cairo_show_layout (cr, layout);

      g_object_unref (layout);
    }
}

static GtkSizeRequestMode
sysprof_cell_renderer_duration_get_request_mode (GtkCellRenderer *renderer)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
sysprof_cell_renderer_duration_get_preferred_width (GtkCellRenderer *cell,
                                                    GtkWidget       *widget,
                                                    gint            *min_width,
                                                    gint            *nat_width)
{
  SysprofCellRendererDuration *self = (SysprofCellRendererDuration *)cell;
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);
  gint width = 1;

  g_assert (SYSPROF_IS_CELL_RENDERER_DURATION (self));
  g_assert (GTK_IS_WIDGET (widget));

  GTK_CELL_RENDERER_CLASS (sysprof_cell_renderer_duration_parent_class)->get_preferred_width (cell, widget, min_width, nat_width);

  if (priv->zoom_manager && priv->capture_begin_time && priv->capture_end_time)
    width = sysprof_zoom_manager_get_width_for_duration (priv->zoom_manager,
                                                         priv->capture_end_time - priv->capture_begin_time);

  if (min_width)
    *min_width = width;

  if (nat_width)
    *nat_width = width;
}

static void
sysprof_cell_renderer_duration_get_preferred_height_for_width (GtkCellRenderer *cell,
                                                               GtkWidget       *widget,
                                                               gint             width,
                                                               gint            *min_height,
                                                               gint            *nat_height)
{
  PangoLayout *layout;
  gint w, h;
  gint ypad;

  g_assert (SYSPROF_IS_CELL_RENDERER_DURATION (cell));

  gtk_cell_renderer_get_padding (cell, NULL, &ypad);

  layout = gtk_widget_create_pango_layout (widget, "XMZ09");
  pango_layout_get_pixel_size (layout, &w, &h);
  g_clear_object (&layout);

  if (min_height)
    *min_height = h + (ypad * 2);

  if (nat_height)
    *nat_height = h + (ypad * 2);
}

static void
sysprof_cell_renderer_duration_finalize (GObject *object)
{
  SysprofCellRendererDuration *self = (SysprofCellRendererDuration *)object;
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);

  g_clear_object (&priv->zoom_manager);
  g_clear_pointer (&priv->text, g_free);

  G_OBJECT_CLASS (sysprof_cell_renderer_duration_parent_class)->finalize (object);
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

    case PROP_CAPTURE_BEGIN_TIME:
      g_value_set_int64 (value, priv->capture_begin_time);
      break;

    case PROP_CAPTURE_END_TIME:
      g_value_set_int64 (value, priv->capture_end_time);
      break;

    case PROP_END_TIME:
      g_value_set_int64 (value, priv->end_time);
      break;

    case PROP_TEXT:
      g_value_set_string (value, priv->text);
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, priv->zoom_manager);
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

    case PROP_CAPTURE_BEGIN_TIME:
      priv->capture_begin_time = g_value_get_int64 (value);
      priv->capture_duration = priv->capture_end_time - priv->capture_begin_time;
      break;

    case PROP_CAPTURE_END_TIME:
      priv->capture_end_time = g_value_get_int64 (value);
      priv->capture_duration = priv->capture_end_time - priv->capture_begin_time;
      break;

    case PROP_COLOR:
      if (g_value_get_boxed (value))
        priv->color = *(GdkRGBA *)g_value_get_boxed (value);
      else
        gdk_rgba_parse (&priv->color, "#000");
      priv->color_set = !!g_value_get_boolean (value);
      break;

    case PROP_END_TIME:
      priv->end_time = g_value_get_int64 (value);
      break;

    case PROP_TEXT:
      g_free (priv->text);
      priv->text = g_value_dup_string (value);
      break;

    case PROP_ZOOM_MANAGER:
      g_set_object (&priv->zoom_manager, g_value_get_object (value));
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

  object_class->finalize = sysprof_cell_renderer_duration_finalize;
  object_class->get_property = sysprof_cell_renderer_duration_get_property;
  object_class->set_property = sysprof_cell_renderer_duration_set_property;

  cell_class->get_preferred_height_for_width = sysprof_cell_renderer_duration_get_preferred_height_for_width;
  cell_class->get_preferred_width = sysprof_cell_renderer_duration_get_preferred_width;
  cell_class->get_request_mode = sysprof_cell_renderer_duration_get_request_mode;
  cell_class->render = sysprof_cell_renderer_duration_render;

  /* Note we do not emit ::notify() for these properties */

  properties [PROP_BEGIN_TIME] =
    g_param_spec_int64 ("begin-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAPTURE_BEGIN_TIME] =
    g_param_spec_int64 ("capture-begin-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAPTURE_END_TIME] =
    g_param_spec_int64 ("capture-end-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_COLOR] =
    g_param_spec_boxed ("color", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TEXT] =
    g_param_spec_string ("text", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager", NULL, NULL,
                         SYSPROF_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_cell_renderer_duration_init (SysprofCellRendererDuration *self)
{
  SysprofCellRendererDurationPrivate *priv = sysprof_cell_renderer_duration_get_instance_private (self);

  priv->color.alpha = 1.0;
}

GtkCellRenderer *
sysprof_cell_renderer_duration_new (void)
{
  return g_object_new (SYSPROF_TYPE_CELL_RENDERER_DURATION, NULL);
}
