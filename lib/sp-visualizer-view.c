/* sp-visualizer-view.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-visualizer-view"

#include <glib/gi18n.h>

#include "sp-theme-manager.h"
#include "sp-visualizer-list.h"
#include "sp-visualizer-row.h"
#include "sp-visualizer-row-private.h"
#include "sp-visualizer-selection.h"
#include "sp-visualizer-ticks.h"
#include "sp-visualizer-view.h"

#define NSEC_PER_SEC G_GINT64_CONSTANT(1000000000)
#define DEFAULT_PIXELS_PER_SECOND 20

typedef struct
{
  SpCaptureReader       *reader;
  SpZoomManager         *zoom_manager;
  SpVisualizerSelection *selection;

  SpVisualizerList      *list;
  GtkScrolledWindow     *scroller;
  SpVisualizerTicks     *ticks;

  gint64                 drag_begin_at;
  gint64                 drag_selection_at;

  guint                  button_pressed : 1;
} SpVisualizerViewPrivate;

typedef struct
{
  SpVisualizerView *self;
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

G_DEFINE_TYPE_EXTENDED (SpVisualizerView, sp_visualizer_view, GTK_TYPE_BIN, 0,
                        G_ADD_PRIVATE (SpVisualizerView)
                        G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];
static GtkBuildableIface *parent_buildable;

static void
find_row1 (GtkWidget *widget,
           gpointer   data)
{
  GtkWidget **row1 = data;

  if (*row1 == NULL && SP_IS_VISUALIZER_ROW (widget))
    *row1 = widget;
}

static gint64
get_time_from_coordinates (SpVisualizerView *self,
                           gint              x,
                           gint              y)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);
  SpVisualizerRow *row1 = NULL;
  GtkAllocation alloc;
  gint64 begin_time;
  gint64 end_time;
  gint graph_width;

  g_assert (SP_IS_VISUALIZER_VIEW (self));

  if (priv->reader == NULL)
    return 0;

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  x -= alloc.x;
  y -= alloc.y;

  /*
   * Find the first row so we can get an idea of how wide the graph is
   * (ignoring spacing caused by the widget being wider than the data points.
   */
  gtk_container_foreach (GTK_CONTAINER (priv->list), find_row1, &row1);
  if (!SP_IS_VISUALIZER_ROW (row1))
    return 0;

  begin_time = sp_capture_reader_get_start_time (priv->reader);
  end_time = sp_capture_reader_get_end_time (priv->reader);
  graph_width = _sp_visualizer_row_get_graph_width (row1);

  return begin_time + ((end_time - begin_time) * (x / (gdouble)graph_width));
}

static gint
get_x_for_time_at (SpVisualizerView    *self,
                   const GtkAllocation *alloc,
                   gint64               time_at)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);
  SpVisualizerRow *row1 = NULL;
  GtkAdjustment *hadjustment;
  gdouble nsec_per_pixel;
  gdouble value;
  gint64 begin_time;
  gint64 end_time;
  gint graph_width;

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (alloc != NULL);

  /*
   * Find the first row so we can get an idea of how wide the graph is
   * (ignoring spacing caused by the widget being wider than the data points.
   */
  gtk_container_foreach (GTK_CONTAINER (priv->list), find_row1, &row1);
  if (!SP_IS_VISUALIZER_ROW (row1))
    return 0;

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);
  value = gtk_adjustment_get_value (hadjustment);

  begin_time = sp_capture_reader_get_start_time (priv->reader);
  end_time = sp_capture_reader_get_end_time (priv->reader);

  graph_width = _sp_visualizer_row_get_graph_width (row1);
  nsec_per_pixel = (end_time - begin_time) / (gdouble)graph_width;
  begin_time += value * nsec_per_pixel;

  return ((time_at - begin_time) / nsec_per_pixel);
}

static void
sp_visualizer_view_row_added (SpVisualizerView *self,
                              GtkWidget        *widget,
                              SpVisualizerList *list)
{
  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SP_IS_VISUALIZER_LIST (list));

  if (SP_IS_VISUALIZER_ROW (widget))
    g_signal_emit (self, signals [VISUALIZER_ADDED], 0, widget);
}

static void
sp_visualizer_view_row_removed (SpVisualizerView *self,
                                GtkWidget        *widget,
                                SpVisualizerList *list)
{
  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SP_IS_VISUALIZER_LIST (list));

  if (SP_IS_VISUALIZER_ROW (widget))
    g_signal_emit (self, signals [VISUALIZER_REMOVED], 0, widget);
}

static void
sp_visualizer_view_update_ticks (SpVisualizerView *self)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);
  GtkAdjustment *hadjustment;
  GtkAllocation alloc;
  gdouble value;
  gint64 begin_time;
  gint64 end_time;

  g_assert (SP_IS_VISUALIZER_VIEW (self));

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);
  value = gtk_adjustment_get_value (hadjustment);

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  begin_time = get_time_from_coordinates (self, alloc.x + value, alloc.y);
  end_time = get_time_from_coordinates (self, alloc.x + value + alloc.width, alloc.y);

  sp_visualizer_ticks_set_time_range (priv->ticks, begin_time, end_time);
}

static void
sp_visualizer_view_hadjustment_value_changed (SpVisualizerView *self,
                                              GtkAdjustment    *adjustment)
{
  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (GTK_IS_ADJUSTMENT (adjustment));

  sp_visualizer_view_update_ticks (self);
}

static void
sp_visualizer_view_size_allocate (GtkWidget     *widget,
                                  GtkAllocation *allocation)
{
  SpVisualizerView *self = (SpVisualizerView *)widget;

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (allocation != NULL);

  GTK_WIDGET_CLASS (sp_visualizer_view_parent_class)->size_allocate (widget, allocation);

  sp_visualizer_view_update_ticks (self);
}

static void
draw_selection_cb (SpVisualizerSelection *selection,
                   gint64                 range_begin,
                   gint64                 range_end,
                   gpointer               user_data)
{
  SelectionDraw *draw = user_data;
  GdkRectangle area;

  g_assert (SP_IS_VISUALIZER_SELECTION (selection));
  g_assert (draw != NULL);
  g_assert (draw->cr != NULL);
  g_assert (SP_IS_VISUALIZER_VIEW (draw->self));

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
sp_visualizer_view_draw (GtkWidget *widget,
                         cairo_t   *cr)
{
  SpVisualizerView *self = (SpVisualizerView *)widget;
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);
  SelectionDraw draw = { 0 };
  gboolean ret;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (cr != NULL);

  draw.style_context = gtk_widget_get_style_context (widget);
  draw.self = self;
  draw.cr = cr;

  gtk_widget_get_allocation (widget, &draw.alloc);

  ret = GTK_WIDGET_CLASS (sp_visualizer_view_parent_class)->draw (widget, cr);

  if (sp_visualizer_selection_get_has_selection (priv->selection) || priv->button_pressed)
    {
      gtk_style_context_add_class (draw.style_context, "selection");
      sp_visualizer_selection_foreach (priv->selection, draw_selection_cb, &draw);
      if (priv->button_pressed)
        draw_selection_cb (priv->selection, priv->drag_begin_at, priv->drag_selection_at, &draw);
      gtk_style_context_remove_class (draw.style_context, "selection");
    }

  return ret;
}

static gboolean
sp_visualizer_view_list_button_press_event (SpVisualizerView *self,
                                            GdkEventButton   *ev,
                                            SpVisualizerList *list)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SP_IS_VISUALIZER_LIST (list));

  if (priv->reader == NULL)
    return GDK_EVENT_PROPAGATE;

  if (ev->button != GDK_BUTTON_PRIMARY)
    {
      if (sp_visualizer_selection_get_has_selection (priv->selection))
        {
          sp_visualizer_selection_unselect_all (priv->selection);
          return GDK_EVENT_STOP;
        }
      return GDK_EVENT_PROPAGATE;
    }

  if ((ev->state & GDK_SHIFT_MASK) == 0)
    sp_visualizer_selection_unselect_all (priv->selection);

  priv->button_pressed = TRUE;

  priv->drag_begin_at = get_time_from_coordinates (self, ev->x, ev->y);
  priv->drag_selection_at = priv->drag_begin_at;

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_PROPAGATE;
}

static gboolean
sp_visualizer_view_list_button_release_event (SpVisualizerView *self,
                                              GdkEventButton   *ev,
                                              SpVisualizerList *list)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SP_IS_VISUALIZER_LIST (list));

  if (!priv->button_pressed || ev->button != GDK_BUTTON_PRIMARY)
    return GDK_EVENT_PROPAGATE;

  priv->button_pressed = FALSE;

  if (priv->drag_begin_at != priv->drag_selection_at)
    {
      sp_visualizer_selection_select_range (priv->selection,
                                            priv->drag_begin_at,
                                            priv->drag_selection_at);
      priv->drag_begin_at = -1;
      priv->drag_selection_at = -1;
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_STOP;
}

static gboolean
sp_visualizer_view_list_motion_notify_event (SpVisualizerView *self,
                                             GdkEventMotion   *ev,
                                             SpVisualizerList *list)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (ev != NULL);
  g_assert (SP_IS_VISUALIZER_LIST (list));

  if (!priv->button_pressed)
    return GDK_EVENT_PROPAGATE;

  priv->drag_selection_at = get_time_from_coordinates (self, ev->x, ev->y);

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return GDK_EVENT_PROPAGATE;
}

static void
sp_visualizer_view_list_realize_after (SpVisualizerView *self,
                                       SpVisualizerList *list)
{
  GdkDisplay *display;
  GdkWindow *window;
  GdkCursor *cursor;

  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (SP_IS_VISUALIZER_LIST (list));

  window = gtk_widget_get_window (GTK_WIDGET (list));
  display = gdk_window_get_display (window);
  cursor = gdk_cursor_new_from_name (display, "text");
  gdk_window_set_cursor (window, cursor);
  g_clear_object (&cursor);
}

static void
sp_visualizer_view_selection_changed (SpVisualizerView      *self,
                                      SpVisualizerSelection *selection)
{
  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (SP_IS_VISUALIZER_SELECTION (selection));

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sp_visualizer_view_finalize (GObject *object)
{
  SpVisualizerView *self = (SpVisualizerView *)object;
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_clear_pointer (&priv->reader, sp_capture_reader_unref);
  g_clear_object (&priv->zoom_manager);
  g_clear_object (&priv->selection);

  G_OBJECT_CLASS (sp_visualizer_view_parent_class)->finalize (object);
}

static void
sp_visualizer_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SpVisualizerView *self = (SpVisualizerView *)object;

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sp_visualizer_view_get_reader (self));
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sp_visualizer_view_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SpVisualizerView *self = (SpVisualizerView *)object;

  switch (prop_id)
    {
    case PROP_READER:
      sp_visualizer_view_set_reader (self, g_value_get_boxed (value));
      break;

    case PROP_ZOOM_MANAGER:
      sp_visualizer_view_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_view_class_init (SpVisualizerViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SpThemeManager *theme_manager = sp_theme_manager_get_default ();

  object_class->finalize = sp_visualizer_view_finalize;
  object_class->get_property = sp_visualizer_view_get_property;
  object_class->set_property = sp_visualizer_view_set_property;

  widget_class->draw = sp_visualizer_view_draw;
  widget_class->size_allocate = sp_visualizer_view_size_allocate;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The reader for the visualizers",
                        SP_TYPE_CAPTURE_READER,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "The zoom manager for the view",
                         SP_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [VISUALIZER_ADDED] =
    g_signal_new ("visualizer-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SpVisualizerViewClass, visualizer_added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, SP_TYPE_VISUALIZER_ROW);

  signals [VISUALIZER_REMOVED] =
    g_signal_new ("visualizer-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SpVisualizerViewClass, visualizer_removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, SP_TYPE_VISUALIZER_ROW);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sp-visualizer-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SpVisualizerView, list);
  gtk_widget_class_bind_template_child_private (widget_class, SpVisualizerView, scroller);
  gtk_widget_class_bind_template_child_private (widget_class, SpVisualizerView, ticks);

  gtk_widget_class_set_css_name (widget_class, "visualizers");

  sp_theme_manager_register_resource (theme_manager, NULL, NULL, "/org/gnome/sysprof/css/SpVisualizerView-shared.css");
  sp_theme_manager_register_resource (theme_manager, "Adwaita", NULL, "/org/gnome/sysprof/css/SpVisualizerView-Adwaita.css");
  sp_theme_manager_register_resource (theme_manager, "Adwaita", "dark", "/org/gnome/sysprof/css/SpVisualizerView-Adwaita-dark.css");
}

static void
sp_visualizer_view_init (SpVisualizerView *self)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);
  GtkAdjustment *hadjustment;

  priv->drag_begin_at = -1;
  priv->drag_selection_at = -1;

  gtk_widget_init_template (GTK_WIDGET (self));

  priv->selection = g_object_new (SP_TYPE_VISUALIZER_SELECTION, NULL);

  g_signal_connect_object (priv->selection,
                           "changed",
                           G_CALLBACK (sp_visualizer_view_selection_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "button-press-event",
                           G_CALLBACK (sp_visualizer_view_list_button_press_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "button-release-event",
                           G_CALLBACK (sp_visualizer_view_list_button_release_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "motion-notify-event",
                           G_CALLBACK (sp_visualizer_view_list_motion_notify_event),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "realize",
                           G_CALLBACK (sp_visualizer_view_list_realize_after),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  g_signal_connect_object (priv->list,
                           "add",
                           G_CALLBACK (sp_visualizer_view_row_added),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->list,
                           "remove",
                           G_CALLBACK (sp_visualizer_view_row_removed),
                           self,
                           G_CONNECT_SWAPPED);

  hadjustment = gtk_scrolled_window_get_hadjustment (priv->scroller);

  g_signal_connect_object (hadjustment,
                           "value-changed",
                           G_CALLBACK (sp_visualizer_view_hadjustment_value_changed),
                           self,
                           G_CONNECT_SWAPPED);
}

/**
 * sp_visualizer_view_get_reader:
 *
 * Returns: (transfer none): An #SpCaptureReader
 */
SpCaptureReader *
sp_visualizer_view_get_reader (SpVisualizerView *self)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_VIEW (self), NULL);

  return priv->reader;
}

void
sp_visualizer_view_set_reader (SpVisualizerView *self,
                               SpCaptureReader  *reader)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_VIEW (self));

  if (priv->reader != reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);

      if (reader != NULL)
        {
          gint64 begin_time;

          priv->reader = sp_capture_reader_ref (reader);

          begin_time = sp_capture_reader_get_start_time (priv->reader);

          sp_visualizer_ticks_set_epoch (priv->ticks, begin_time);
          sp_visualizer_ticks_set_time_range (priv->ticks, begin_time, begin_time);

          sp_visualizer_selection_unselect_all (priv->selection);
        }

      sp_visualizer_list_set_reader (priv->list, reader);
      sp_visualizer_view_update_ticks (self);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}

static void
sp_visualizer_view_add_child (GtkBuildable *buildable,
                              GtkBuilder   *builder,
                              GObject      *child,
                              const gchar  *type)
{
  SpVisualizerView *self = (SpVisualizerView *)buildable;
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_assert (SP_IS_VISUALIZER_VIEW (self));
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
  iface->add_child = sp_visualizer_view_add_child;
}

static void
sp_visualizer_view_zoom_manager_notify_zoom (SpVisualizerView *self,
                                             GParamSpec       *pspec,
                                             SpZoomManager    *zoom_manager)
{
  g_assert (SP_IS_VISUALIZER_VIEW (self));
  g_assert (SP_IS_ZOOM_MANAGER (zoom_manager));

  sp_visualizer_view_update_ticks (self);
}

/**
 * sp_visualizer_view_get_zoom_manager:
 *
 * Returns: (transfer none) (nullable): An #SpZoomManager or %NULL
 */
SpZoomManager *
sp_visualizer_view_get_zoom_manager (SpVisualizerView *self)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_VIEW (self), NULL);

  return priv->zoom_manager;
}

void
sp_visualizer_view_set_zoom_manager (SpVisualizerView *self,
                                     SpZoomManager    *zoom_manager)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_VIEW (self));
  g_return_if_fail (!zoom_manager || SP_IS_ZOOM_MANAGER (zoom_manager));

  if (priv->zoom_manager != zoom_manager)
    {
      if (priv->zoom_manager != NULL)
        {
          g_signal_handlers_disconnect_by_func (priv->zoom_manager,
                                                G_CALLBACK (sp_visualizer_view_zoom_manager_notify_zoom),
                                                self);
          g_clear_object (&priv->zoom_manager);
        }

      if (zoom_manager != NULL)
        {
          priv->zoom_manager = g_object_ref (zoom_manager);
          g_signal_connect_object (priv->zoom_manager,
                                   "notify::zoom",
                                   G_CALLBACK (sp_visualizer_view_zoom_manager_notify_zoom),
                                   self,
                                   G_CONNECT_SWAPPED);
        }

      sp_visualizer_list_set_zoom_manager (priv->list, zoom_manager);
      gtk_widget_queue_resize (GTK_WIDGET (self));

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
    }
}

/**
 * sp_visualizer_view_get_selection:
 *
 * Gets the #SpVisualizerSelection instance for the visualizer view.
 * This can be used to alter the selection or selections of the visualizers.
 *
 * Returns: (transfer none): An #SpVisualizerSelection.
 */
SpVisualizerSelection *
sp_visualizer_view_get_selection (SpVisualizerView *self)
{
  SpVisualizerViewPrivate *priv = sp_visualizer_view_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_VIEW (self), NULL);

  return priv->selection;
}
