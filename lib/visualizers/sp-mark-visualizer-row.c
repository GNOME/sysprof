/* sp-mark-visualizer-row.c
 *
 * Copyright 2018 Christian Hergert <chergert@redhat.com>
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
 */

#define G_LOG_DOMAIN "sp-mark-visualizer-row"

#include "sp-mark-visualizer-row.h"

typedef struct
{
  /*
   * Our reader as assigned by the visualizer system.
   */
  SpCaptureReader *reader;

  /*
   * A sorted array of MarkInfo about marks we care to render.
   */
  GArray *marks;

  /*
   * Child widget to display the label in the upper corner.
   */
  GtkLabel *label;
} SpMarkVisualizerRowPrivate;

typedef struct
{
  gchar *name;
  GPid   pid;
} MarkInfo;

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpMarkVisualizerRow, sp_mark_visualizer_row, SP_TYPE_VISUALIZER_ROW)

static GParamSpec *properties [N_PROPS];

static gboolean
sp_mark_visualizer_row_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
#if 0
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)widget;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);
  GtkStyleContext *style_context;
  GtkStateFlags flags;
  GdkRGBA foreground;
#endif
  GtkAllocation alloc;
  gboolean ret;

  g_assert (SP_IS_MARK_VISUALIZER_ROW (widget));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sp_mark_visualizer_row_parent_class)->draw (widget, cr);

#if 0
  if (priv->cache == NULL)
    return ret;

  style_context = gtk_widget_get_style_context (widget);
  flags = gtk_widget_get_state_flags (widget);
  gtk_style_context_get_color (style_context, flags, &foreground);

  for (guint line = 0; line < priv->lines->len; line++)
    {
      g_autofree SpVisualizerRowAbsolutePoint *points = NULL;
      const MarkInfo *line_info = &g_array_index (priv->lines, MarkInfo, line);
      const Point *fpoints;
      guint n_fpoints = 0;
      GdkRGBA color;

      fpoints = point_cache_get_points (priv->cache, line_info->id, &n_fpoints);

      if (n_fpoints > 0)
        {
          gfloat last_x;
          gfloat last_y;

          points = g_new0 (SpVisualizerRowAbsolutePoint, n_fpoints);

          sp_visualizer_row_translate_points (SP_VISUALIZER_ROW (self),
                                              (const SpVisualizerRowRelativePoint *)fpoints,
                                              n_fpoints,
                                              points,
                                              n_fpoints);

          last_x = points[0].x;
          last_y = points[0].y;

          if (line_info->fill)
            {
              cairo_move_to (cr, last_x, alloc.height);
              cairo_line_to (cr, last_x, last_y);
            }
          else
            {
              cairo_move_to (cr, last_x, last_y);
            }

          for (guint i = 1; i < n_fpoints; i++)
            {
              cairo_curve_to (cr,
                              last_x + ((points[i].x - last_x) / 2),
                              last_y,
                              last_x + ((points[i].x - last_x) / 2),
                              points[i].y,
                              points[i].x,
                              points[i].y);
              last_x = points[i].x;
              last_y = points[i].y;
            }

          if (line_info->fill)
            {
              cairo_line_to (cr, last_x, alloc.height);
              cairo_close_path (cr);
            }

          cairo_set_line_width (cr, line_info->line_width);

          if (line_info->use_default_style)
            color = foreground;
          else
            color = line_info->foreground;

          gdk_cairo_set_source_rgba (cr, &color);

          if (line_info->fill)
            cairo_fill (cr);
          else
            cairo_stroke (cr);
        }
    }
#endif

  return ret;
}

static void
sp_mark_visualizer_row_queue_reload (SpMarkVisualizerRow *self)
{
  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));

}

static void
sp_mark_visualizer_row_set_reader (SpVisualizerRow *row,
                                   SpCaptureReader *reader)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)row;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));

  if (reader != priv->reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);
      if (reader != NULL)
        priv->reader = sp_capture_reader_ref (reader);
      sp_mark_visualizer_row_queue_reload (self);
    }
}

static void
sp_mark_visualizer_row_finalize (GObject *object)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)object;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->marks, g_array_unref);

  G_OBJECT_CLASS (sp_mark_visualizer_row_parent_class)->finalize (object);
}

static void
sp_mark_visualizer_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpMarkVisualizerRow *self = SP_MARK_VISUALIZER_ROW (object);
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, gtk_label_get_label (priv->label));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_mark_visualizer_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SpMarkVisualizerRow *self = SP_MARK_VISUALIZER_ROW (object);
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      gtk_label_set_label (priv->label, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_mark_visualizer_row_class_init (SpMarkVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SpVisualizerRowClass *visualizer_class = SP_VISUALIZER_ROW_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sp_mark_visualizer_row_finalize;
  object_class->get_property = sp_mark_visualizer_row_get_property;
  object_class->set_property = sp_mark_visualizer_row_set_property;

  widget_class->draw = sp_mark_visualizer_row_draw;

  visualizer_class->set_reader = sp_mark_visualizer_row_set_reader;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_mark_visualizer_row_init (SpMarkVisualizerRow *self)
{
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);
  PangoAttrList *attrs = pango_attr_list_new ();

  priv->marks = g_array_new (FALSE, FALSE, sizeof (MarkInfo));

  pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL * PANGO_SCALE_SMALL));

  priv->label = g_object_new (GTK_TYPE_LABEL,
                              "attributes", attrs,
                              "visible", TRUE,
                              "xalign", 0.0f,
                              "yalign", 0.0f,
                              NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (priv->label));

  pango_attr_list_unref (attrs);
}

GtkWidget *
sp_mark_visualizer_row_new (void)
{
  return g_object_new (SP_TYPE_MARK_VISUALIZER_ROW, NULL);
}

void
sp_mark_visualizer_row_add_mark (SpMarkVisualizerRow *self,
                                 GPid                 pid,
                                 GPid                 tid,
                                 const gchar         *name,
                                 const GdkRGBA       *color)
{
  g_return_if_fail (SP_IS_MARK_VISUALIZER_ROW (self));
  g_return_if_fail (name != NULL);

}
