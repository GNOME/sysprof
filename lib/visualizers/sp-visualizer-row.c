/* sp-visualizer-row.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "sp-visualizer-row"

#include "visualizers/sp-visualizer-row.h"
#include "visualizers/sp-visualizer-row-private.h"

#define NSEC_PER_SEC              G_GINT64_CONSTANT(1000000000)
#define DEFAULT_PIXELS_PER_SECOND 20

typedef struct
{
  SpCaptureReader *reader;
  SpZoomManager   *zoom_manager;
} SpVisualizerRowPrivate;

enum {
  PROP_0,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SpVisualizerRow, sp_visualizer_row, GTK_TYPE_LIST_BOX_ROW)

gint
_sp_visualizer_row_get_graph_width (SpVisualizerRow *self)
{
  SpVisualizerRowPrivate *priv = sp_visualizer_row_get_instance_private (self);
  gdouble zoom_level = 1.0;
  gint64 begin_time;
  gint64 end_time;

  g_assert (SP_IS_VISUALIZER_ROW (self));

  if (priv->reader == NULL)
    return 0;

  if (priv->zoom_manager != NULL)
    zoom_level = sp_zoom_manager_get_zoom (priv->zoom_manager);

  begin_time = sp_capture_reader_get_start_time (priv->reader);
  end_time = sp_capture_reader_get_end_time (priv->reader);

  return (end_time - begin_time)
         / (gdouble)NSEC_PER_SEC
         * zoom_level
         * DEFAULT_PIXELS_PER_SECOND;
}

static void
sp_visualizer_row_get_preferred_width (GtkWidget *widget,
                                       gint      *min_width,
                                       gint      *nat_width)
{
  SpVisualizerRow *self = (SpVisualizerRow *)widget;
  gint graph_width;
  gint real_min_width = 0;
  gint real_nat_width = 0;

  g_assert (SP_IS_VISUALIZER_ROW (self));

  GTK_WIDGET_CLASS (sp_visualizer_row_parent_class)->get_preferred_width (widget, &real_min_width, &real_nat_width);

  graph_width = _sp_visualizer_row_get_graph_width (self);

  *min_width = *nat_width = real_min_width + graph_width;
}

static void
sp_visualizer_row_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SpVisualizerRow *self = SP_VISUALIZER_ROW (object);

  switch (prop_id)
    {
    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sp_visualizer_row_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_row_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SpVisualizerRow *self = SP_VISUALIZER_ROW (object);

  switch (prop_id)
    {
    case PROP_ZOOM_MANAGER:
      sp_visualizer_row_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_row_finalize (GObject *object)
{
  SpVisualizerRow *self = (SpVisualizerRow *)object;
  SpVisualizerRowPrivate *priv = sp_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->reader, sp_capture_reader_unref);
  g_clear_object (&priv->zoom_manager);

  G_OBJECT_CLASS (sp_visualizer_row_parent_class)->finalize (object);
}

static void
sp_visualizer_row_class_init (SpVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sp_visualizer_row_finalize;
  object_class->get_property = sp_visualizer_row_get_property;
  object_class->set_property = sp_visualizer_row_set_property;

  widget_class->get_preferred_width = sp_visualizer_row_get_preferred_width;

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "Zoom Manager",
                         SP_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_visualizer_row_init (SpVisualizerRow *self)
{
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self), FALSE);
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (self), FALSE);
}

static void
sp_visualizer_row_zoom_manager_notify_zoom (SpVisualizerRow *self,
                                            GParamSpec      *pspec,
                                            SpZoomManager   *zoom_manager)
{
  g_assert (SP_IS_VISUALIZER_ROW (self));
  g_assert (SP_IS_ZOOM_MANAGER (zoom_manager));

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * sp_visualizer_row_get_zoom_manager:
 *
 * Returns: (transfer none) (nullable): A #SpZoomManager or %NULL.
 */
SpZoomManager *
sp_visualizer_row_get_zoom_manager (SpVisualizerRow *self)
{
  SpVisualizerRowPrivate *priv = sp_visualizer_row_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_ROW (self), NULL);

  return priv->zoom_manager;
}

void
sp_visualizer_row_set_zoom_manager (SpVisualizerRow *self,
                                    SpZoomManager   *zoom_manager)
{
  SpVisualizerRowPrivate *priv = sp_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_ROW (self));
  g_return_if_fail (!zoom_manager || SP_IS_ZOOM_MANAGER (zoom_manager));

  if (priv->zoom_manager != zoom_manager)
    {
      if (priv->zoom_manager != NULL)
        {
          g_signal_handlers_disconnect_by_func (priv->zoom_manager,
                                                G_CALLBACK (sp_visualizer_row_zoom_manager_notify_zoom),
                                                self);
          g_clear_object (&priv->zoom_manager);
        }

      if (zoom_manager != NULL)
        {
          priv->zoom_manager = g_object_ref (zoom_manager);
          g_signal_connect_object (priv->zoom_manager,
                                   "notify::zoom",
                                   G_CALLBACK (sp_visualizer_row_zoom_manager_notify_zoom),
                                   self,
                                   G_CONNECT_SWAPPED);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
      gtk_widget_queue_resize (GTK_WIDGET (self));
    }
}

void
sp_visualizer_row_set_reader (SpVisualizerRow *self,
                              SpCaptureReader *reader)
{
  SpVisualizerRowPrivate *priv = sp_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_ROW (self));

  if (priv->reader != reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);

      if (reader != NULL)
        priv->reader = sp_capture_reader_ref (reader);

      if (SP_VISUALIZER_ROW_GET_CLASS (self)->set_reader)
        SP_VISUALIZER_ROW_GET_CLASS (self)->set_reader (self, reader);

      gtk_widget_queue_resize (GTK_WIDGET (self));
    }
}

static inline void
subtract_border (GtkAllocation *alloc,
                 GtkBorder     *border)
{
#if 0
  g_print ("Border; %d %d %d %d\n", border->top, border->left, border->bottom, border->right);
#endif

  alloc->x += border->left;
  alloc->y += border->top;
  alloc->width -= border->left + border->right;
  alloc->height -= border->top + border->bottom;
}

static void
adjust_alloc_for_borders (SpVisualizerRow *self,
                          GtkAllocation   *alloc)
{
  GtkStyleContext *style_context;
  GtkBorder border;
  GtkStateFlags state;

  g_assert (SP_IS_VISUALIZER_ROW (self));
  g_assert (alloc != NULL);

  state = gtk_widget_get_state_flags (GTK_WIDGET (self));
  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_get_border (style_context, state, &border);

  subtract_border (alloc, &border);
}

void
sp_visualizer_row_translate_points (SpVisualizerRow                    *self,
                                    const SpVisualizerRowRelativePoint *in_points,
                                    guint                               n_in_points,
                                    SpVisualizerRowAbsolutePoint       *out_points,
                                    guint                               n_out_points)
{
  GtkAllocation alloc;
  gint graph_width;

  g_return_if_fail (SP_IS_VISUALIZER_ROW (self));
  g_return_if_fail (in_points != NULL);
  g_return_if_fail (out_points != NULL);
  g_return_if_fail (n_in_points == n_out_points);

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);
  adjust_alloc_for_borders (self, &alloc);

  graph_width = _sp_visualizer_row_get_graph_width (self);

  for (guint i = 0; i < n_in_points; i++)
    {
      out_points[i].x = alloc.x + (in_points[i].x * graph_width);
      out_points[i].y = alloc.y + alloc.height - (in_points[i].y * alloc.height);
    }
}

