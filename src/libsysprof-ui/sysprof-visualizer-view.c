/* sysprof-visualizer-view.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-visualizer-view"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-theme-manager.h"
#include "sysprof-visualizer-list.h"
#include "sysprof-visualizer-row.h"
#include "sysprof-visualizer-row-private.h"
#include "sysprof-visualizer-ticks.h"
#include "sysprof-visualizer-view.h"

typedef struct
{
  SysprofCaptureReader       *reader;
  SysprofZoomManager         *zoom_manager;
  SysprofSelection           *selection;

  SysprofVisualizerList      *list;
  GtkScrolledWindow          *scroller;
  SysprofVisualizerTicks     *ticks;

  gint64                      drag_begin_at;
  gint64                      drag_selection_at;

  guint                       button_pressed : 1;
} SysprofVisualizerViewPrivate;

typedef struct
{
  SysprofVisualizerView *self;
  GtkStyleContext  *style_context;
  cairo_t          *cr;
  GtkAllocation     alloc;
} SelectionDraw;

enum {
  PROP_0,
  PROP_READER,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

enum {
  VISUALIZER_ADDED,
  VISUALIZER_REMOVED,
  N_SIGNALS
};

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofVisualizerView, sysprof_visualizer_view, GTK_TYPE_BIN, 0,
                        G_ADD_PRIVATE (SysprofVisualizerView)
                        G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];
static GtkBuildableIface *parent_buildable;

static void
find_row1 (GtkWidget *widget,
           gpointer   data)
{
  GtkWidget **row1 = data;

  if (*row1 == NULL && SYSPROF_IS_VISUALIZER_ROW (widget))
    *row1 = widget;
}

static gint64
get_time_from_coordinates (SysprofVisualizerView *self,
                           gint                   x,
                           gint                   y)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);
  SysprofVisualizerRow *row1 = NULL;
  gint64 begin_time;
  gint64 end_time;
  gint graph_width;

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));

  if (priv->reader == NULL)
    return 0;

  /*
   * Find the first row so we can get an idea of how wide the graph is
   * (ignoring spacing caused by the widget being wider than the data points.
   */
  gtk_container_foreach (GTK_CONTAINER (priv->list), find_row1, &row1);
  if (!SYSPROF_IS_VISUALIZER_ROW (row1))
    return 0;

  begin_time = sysprof_capture_reader_get_start_time (priv->reader);
  end_time = sysprof_capture_reader_get_end_time (priv->reader);
  graph_width = _sysprof_visualizer_row_get_graph_width (row1);

  return begin_time + ((end_time - begin_time) * (x / (gdouble)graph_width));
}

static gint
get_x_for_time_at (SysprofVisualizerView *self,
                   const GtkAllocation   *alloc,
                   gint64                 time_at)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);
  SysprofVisualizerRow *row1 = NULL;
  GtkAdjustment *hadjustment;
  gdouble nsec_per_pixel;
  gdouble value;
  gint64 begin_time;
  gint64 end_time;
  gint graph_width;

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (alloc != NULL);

  /*
   * Find the first row so we can get an idea of how wide the graph is
   * (ignoring spacing caused by the widget being wider than the data points.
   */
  gtk_container_foreach (GTK_CONTAINER (priv->list), find_row1, &row1);
  if (!SYSPROF_IS_VISUALIZER_ROW (row1))
    return 0;

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);
  value = gtk_adjustment_get_value (hadjustment);

  begin_time = sysprof_capture_reader_get_start_time (priv->reader);
  end_time = sysprof_capture_reader_get_end_time (priv->reader);

  graph_width = _sysprof_visualizer_row_get_graph_width (row1);
  nsec_per_pixel = (end_time - begin_time) / (gdouble)graph_width;
  begin_time += value * nsec_per_pixel;

  return ((time_at - begin_time) / nsec_per_pixel);
}

static void
sysprof_visualizer_view_row_added (SysprofVisualizerView *self,
                                   GtkWidget             *widget,
                                   SysprofVisualizerList *list)
{
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  if (SYSPROF_IS_VISUALIZER_ROW (widget))
    g_signal_emit (self, signals [VISUALIZER_ADDED], 0, widget);
}

static void
sysprof_visualizer_view_row_removed (SysprofVisualizerView *self,
                                     GtkWidget             *widget,
                                     SysprofVisualizerList *list)
{
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  if (SYSPROF_IS_VISUALIZER_ROW (widget))
    g_signal_emit (self, signals [VISUALIZER_REMOVED], 0, widget);
}

static void
sysprof_visualizer_view_update_ticks (SysprofVisualizerView *self)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);
  GtkAdjustment *hadjustment;
  GtkAllocation alloc;
  gdouble value;
  gint64 begin_time;
  gint64 end_time;

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);
  value = gtk_adjustment_get_value (hadjustment);

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  begin_time = get_time_from_coordinates (self, alloc.x + value, alloc.y);
  end_time = get_time_from_coordinates (self, alloc.x + value + alloc.width, alloc.y);

  sysprof_visualizer_ticks_set_time_range (priv->ticks, begin_time, end_time);
}

static void
sysprof_visualizer_view_hadjustment_value_changed (SysprofVisualizerView *self,
                                                   GtkAdjustment         *adjustment)
{
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_ADJUSTMENT (adjustment));

  sysprof_visualizer_view_update_ticks (self);
}

static void
sysprof_visualizer_view_size_allocate (GtkWidget     *widget,
                                       GtkAllocation *allocation)
{
  SysprofVisualizerView *self = (SysprofVisualizerView *)widget;

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (allocation != NULL);

  GTK_WIDGET_CLASS (sysprof_visualizer_view_parent_class)->size_allocate (widget, allocation);

  sysprof_visualizer_view_update_ticks (self);
}

static void
draw_selection_cb (SysprofSelection *selection,
                   gint64            range_begin,
                   gint64            range_end,
                   gpointer          user_data)
{
  SelectionDraw *draw = user_data;
  GdkRectangle area;

  g_assert (SYSPROF_IS_SELECTION (selection));
  g_assert (draw != NULL);
  g_assert (draw->cr != NULL);
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (draw->self));

  area.x = get_x_for_time_at (draw->self, &draw->alloc, range_begin);
  area.width = get_x_for_time_at (draw->self, &draw->alloc, range_end) - area.x;
  area.y = 0;
  area.height = draw->alloc.height;

  if (area.width < 0)
    {
      area.width = ABS (area.width);
      area.x -= area.width;
    }

  gtk_render_background (draw->style_context, draw->cr, area.x, area.y, area.width, area.height);
}

static gboolean
sysprof_visualizer_view_draw (GtkWidget *widget,
                              cairo_t   *cr)
{
  SysprofVisualizerView *self = (SysprofVisualizerView *)widget;
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);
  SelectionDraw draw = { 0 };
  gboolean ret;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (cr != NULL);

  draw.style_context = gtk_widget_get_style_context (widget);
  draw.self = self;
  draw.cr = cr;

  gtk_widget_get_allocation (widget, &draw.alloc);

  ret = GTK_WIDGET_CLASS (sysprof_visualizer_view_parent_class)->draw (widget, cr);

  if (sysprof_selection_get_has_selection (priv->selection) || priv->button_pressed)
    {
      gtk_style_context_add_class (draw.style_context, "selection");
      sysprof_selection_foreach (priv->selection, draw_selection_cb, &draw);
      if (priv->button_pressed)
        draw_selection_cb (priv->selection, priv->drag_begin_at, priv->drag_selection_at, &draw);
      gtk_style_context_remove_class (draw.style_context, "selection");
    }

  return ret;
}

static gboolean
sysprof_visualizer_view_list_button_press_event (SysprofVisualizerView *self,
                                                 GdkEventButton        *ev,
                                                 SysprofVisualizerList *list)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  if (priv->reader == NULL)
    return GDK_EVENT_PROPAGATE;

  if (ev->button != GDK_BUTTON_PRIMARY)
    {
      if (sysprof_selection_get_has_selection (priv->selection))
        {
          sysprof_selection_unselect_all (priv->selection);
          return GDK_EVENT_STOP;
        }
      return GDK_EVENT_PROPAGATE;
    }

  if ((ev->state & GDK_SHIFT_MASK) == 0)
    sysprof_selection_unselect_all (priv->selection);

  priv->button_pressed = TRUE;

  priv->drag_begin_at = get_time_from_coordinates (self, ev->x, ev->y);
  priv->drag_selection_at = priv->drag_begin_at;

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_PROPAGATE;
}

static gboolean
sysprof_visualizer_view_list_button_release_event (SysprofVisualizerView *self,
                                                   GdkEventButton        *ev,
                                                   SysprofVisualizerList *list)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  if (!priv->button_pressed || ev->button != GDK_BUTTON_PRIMARY)
    return GDK_EVENT_PROPAGATE;

  priv->button_pressed = FALSE;

  if (priv->drag_begin_at != priv->drag_selection_at)
    {
      sysprof_selection_select_range (priv->selection,
                                      priv->drag_begin_at,
                                      priv->drag_selection_at);
      priv->drag_begin_at = -1;
      priv->drag_selection_at = -1;
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_STOP;
}

static gboolean
sysprof_visualizer_view_list_motion_notify_event (SysprofVisualizerView *self,
                                                  GdkEventMotion        *ev,
                                                  SysprofVisualizerList *list)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  if (!priv->button_pressed)
    return GDK_EVENT_PROPAGATE;

  priv->drag_selection_at = get_time_from_coordinates (self, ev->x, ev->y);

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_PROPAGATE;
}

static void
sysprof_visualizer_view_list_realize_after (SysprofVisualizerView *self,
                                            SysprofVisualizerList *list)
{
  GdkDisplay *display;
  GdkWindow *window;
  GdkCursor *cursor;

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (SYSPROF_IS_VISUALIZER_LIST (list));

  window = gtk_widget_get_window (GTK_WIDGET (list));
  display = gdk_window_get_display (window);
  cursor = gdk_cursor_new_from_name (display, "text");
  gdk_window_set_cursor (window, cursor);
  g_clear_object (&cursor);
}

static void
sysprof_visualizer_view_selection_changed (SysprofVisualizerView *self,
                                           SysprofSelection      *selection)
{
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (SYSPROF_IS_SELECTION (selection));

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_visualizer_view_finalize (GObject *object)
{
  SysprofVisualizerView *self = (SysprofVisualizerView *)object;
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);
  g_clear_object (&priv->zoom_manager);
  g_clear_object (&priv->selection);

  G_OBJECT_CLASS (sysprof_visualizer_view_parent_class)->finalize (object);
}

static void
sysprof_visualizer_view_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofVisualizerView *self = SYSPROF_VISUALIZER_VIEW (object);

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sysprof_visualizer_view_get_reader (self));
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sysprof_visualizer_view_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_view_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofVisualizerView *self = SYSPROF_VISUALIZER_VIEW (object);

  switch (prop_id)
    {
    case PROP_READER:
      sysprof_visualizer_view_set_reader (self, g_value_get_boxed (value));
      break;

    case PROP_ZOOM_MANAGER:
      sysprof_visualizer_view_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_view_class_init (SysprofVisualizerViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofThemeManager *theme_manager = sysprof_theme_manager_get_default ();

  object_class->finalize = sysprof_visualizer_view_finalize;
  object_class->get_property = sysprof_visualizer_view_get_property;
  object_class->set_property = sysprof_visualizer_view_set_property;

  widget_class->draw = sysprof_visualizer_view_draw;
  widget_class->size_allocate = sysprof_visualizer_view_size_allocate;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The reader for the visualizers",
                        SYSPROF_TYPE_CAPTURE_READER,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "The zoom manager for the view",
                         SYSPROF_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [VISUALIZER_ADDED] =
    g_signal_new ("visualizer-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SysprofVisualizerViewClass, visualizer_added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, SYSPROF_TYPE_VISUALIZER_ROW);

  signals [VISUALIZER_REMOVED] =
    g_signal_new ("visualizer-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SysprofVisualizerViewClass, visualizer_removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, SYSPROF_TYPE_VISUALIZER_ROW);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-visualizer-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofVisualizerView, list);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofVisualizerView, scroller);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofVisualizerView, ticks);

  gtk_widget_class_set_css_name (widget_class, "visualizers");

  sysprof_theme_manager_register_resource (theme_manager, NULL, NULL, "/org/gnome/sysprof/css/SysprofVisualizerView-shared.css");
  sysprof_theme_manager_register_resource (theme_manager, "Adwaita", NULL, "/org/gnome/sysprof/css/SysprofVisualizerView-Adwaita.css");
  sysprof_theme_manager_register_resource (theme_manager, "Adwaita", "dark", "/org/gnome/sysprof/css/SysprofVisualizerView-Adwaita-dark.css");

  g_type_ensure (SYSPROF_TYPE_VISUALIZER_LIST);
  g_type_ensure (SYSPROF_TYPE_VISUALIZER_TICKS);
}

static void
sysprof_visualizer_view_init (SysprofVisualizerView *self)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);
  GtkAdjustment *hadjustment;

  priv->drag_begin_at = -1;
  priv->drag_selection_at = -1;

  gtk_widget_init_template (GTK_WIDGET (self));

  priv->selection = g_object_new (SYSPROF_TYPE_SELECTION, NULL);

  g_signal_connect_object (priv->selection,
                           "changed",
                           G_CALLBACK (sysprof_visualizer_view_selection_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "button-press-event",
                           G_CALLBACK (sysprof_visualizer_view_list_button_press_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "button-release-event",
                           G_CALLBACK (sysprof_visualizer_view_list_button_release_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "motion-notify-event",
                           G_CALLBACK (sysprof_visualizer_view_list_motion_notify_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "realize",
                           G_CALLBACK (sysprof_visualizer_view_list_realize_after),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  g_signal_connect_object (priv->list,
                           "add",
                           G_CALLBACK (sysprof_visualizer_view_row_added),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "remove",
                           G_CALLBACK (sysprof_visualizer_view_row_removed),
                           self,
                           G_CONNECT_SWAPPED);

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);

  g_signal_connect_object (hadjustment,
                           "value-changed",
                           G_CALLBACK (sysprof_visualizer_view_hadjustment_value_changed),
                           self,
                           G_CONNECT_SWAPPED);
}

/**
 * sysprof_visualizer_view_get_reader:
 *
 * Returns: (transfer none): An #SysprofCaptureReader
 */
SysprofCaptureReader *
sysprof_visualizer_view_get_reader (SysprofVisualizerView *self)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self), NULL);

  return priv->reader;
}

void
sysprof_visualizer_view_set_reader (SysprofVisualizerView *self,
                                    SysprofCaptureReader  *reader)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self));

  if (priv->reader != reader)
    {
      g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);

      if (reader != NULL)
        {
          gint64 begin_time;
          gint64 end_time;

          priv->reader = sysprof_capture_reader_ref (reader);

          begin_time = sysprof_capture_reader_get_start_time (priv->reader);
          end_time = sysprof_capture_reader_get_end_time (priv->reader);

          sysprof_visualizer_ticks_set_epoch (priv->ticks, begin_time);
          sysprof_visualizer_ticks_set_time_range (priv->ticks, begin_time, end_time);

          sysprof_selection_unselect_all (priv->selection);
        }

      sysprof_visualizer_list_set_reader (priv->list, reader);
      sysprof_visualizer_view_update_ticks (self);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}

static void
sysprof_visualizer_view_add_child (GtkBuildable *buildable,
                                   GtkBuilder   *builder,
                                   GObject      *child,
                                   const gchar  *type)
{
  SysprofVisualizerView *self = (SysprofVisualizerView *)buildable;
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_BUILDER (builder));
  g_assert (G_IS_OBJECT (child));

  if (g_strcmp0 (type, "visualizer") == 0 && GTK_IS_WIDGET (child))
    {
      gtk_container_add (GTK_CONTAINER (priv->list), GTK_WIDGET (child));
      return;
    }

  parent_buildable->add_child (buildable, builder, child, type);
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = sysprof_visualizer_view_add_child;
}

static void
sysprof_visualizer_view_zoom_manager_notify_zoom (SysprofVisualizerView *self,
                                                  GParamSpec            *pspec,
                                                  SysprofZoomManager    *zoom_manager)
{
  g_assert (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_assert (SYSPROF_IS_ZOOM_MANAGER (zoom_manager));

  sysprof_visualizer_view_update_ticks (self);
}

/**
 * sysprof_visualizer_view_get_zoom_manager:
 *
 * Returns: (transfer none) (nullable): An #SysprofZoomManager or %NULL
 */
SysprofZoomManager *
sysprof_visualizer_view_get_zoom_manager (SysprofVisualizerView *self)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self), NULL);

  return priv->zoom_manager;
}

void
sysprof_visualizer_view_set_zoom_manager (SysprofVisualizerView *self,
                                          SysprofZoomManager    *zoom_manager)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_return_if_fail (!zoom_manager || SYSPROF_IS_ZOOM_MANAGER (zoom_manager));

  if (priv->zoom_manager != zoom_manager)
    {
      if (priv->zoom_manager != NULL)
        {
          g_signal_handlers_disconnect_by_func (priv->zoom_manager,
                                                G_CALLBACK (sysprof_visualizer_view_zoom_manager_notify_zoom),
                                                self);
          g_clear_object (&priv->zoom_manager);
        }

      if (zoom_manager != NULL)
        {
          priv->zoom_manager = g_object_ref (zoom_manager);
          g_signal_connect_object (priv->zoom_manager,
                                   "notify::zoom",
                                   G_CALLBACK (sysprof_visualizer_view_zoom_manager_notify_zoom),
                                   self,
                                   G_CONNECT_SWAPPED);
        }

      sysprof_visualizer_list_set_zoom_manager (priv->list, zoom_manager);
      gtk_widget_queue_resize (GTK_WIDGET (self));

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
    }
}

/**
 * sysprof_visualizer_view_get_selection:
 *
 * Gets the #SysprofSelection instance for the visualizer view.
 * This can be used to alter the selection or selections of the visualizers.
 *
 * Returns: (transfer none): An #SysprofSelection.
 */
SysprofSelection *
sysprof_visualizer_view_get_selection (SysprofVisualizerView *self)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self), NULL);

  return priv->selection;
}

void
_sysprof_visualizer_view_set_hadjustment (SysprofVisualizerView *self,
                                          GtkAdjustment         *hadjustment)
{
  SysprofVisualizerViewPrivate *priv = sysprof_visualizer_view_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_VIEW (self));
  g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));

  gtk_scrolled_window_set_hadjustment (priv->scroller, hadjustment);
}
