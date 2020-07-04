/* sysprof-visualizers-frame.c
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

#define G_LOG_DOMAIN "sysprof-visualizers-frame"

#include "config.h"

#include "sysprof-scrollmap.h"
#include "sysprof-visualizer-group-private.h"
#include "sysprof-visualizer-ticks.h"
#include "sysprof-visualizers-frame.h"
#include "sysprof-zoom-manager.h"

struct _SysprofVisualizersFrame
{
  GtkBin                  parent_instance;

  /* Drag selection tracking */
  SysprofSelection       *selection;
  gint64                  drag_begin_at;
  gint64                  drag_selection_at;
  guint                   button_pressed : 1;

  /* Help avoid over-resizing/allocating */
  GtkAllocation           last_alloc;
  gdouble                 last_zoom;

  /* Known time range from the capture */
  gint64                  begin_time;
  gint64                  end_time;

  /* Template Widgets */
  GtkListBox             *groups;
  GtkListBox             *visualizers;
  SysprofScrollmap       *hscrollbar;
  SysprofVisualizerTicks *ticks;
  GtkScrolledWindow      *ticks_scroller;
  GtkScrolledWindow      *hscroller;
  GtkScrolledWindow      *vscroller;
  SysprofZoomManager     *zoom_manager;
  GtkScale               *zoom_scale;
  GtkSizeGroup           *left_column;
  GtkViewport            *ticks_viewport;
  GtkViewport            *visualizers_viewport;
};

typedef struct
{
  GtkListBox      *list;
  GtkStyleContext *style_context;
  cairo_t         *cr;
  GtkAllocation    alloc;
  gint64           begin_time;
  gint64           duration;
} SelectionDraw;

G_DEFINE_TYPE (SysprofVisualizersFrame, sysprof_visualizers_frame, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_SELECTED_GROUP,
  PROP_SELECTION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static gint64
get_time_from_x (SysprofVisualizersFrame *self,
                 gdouble                  x)
{
  GtkAllocation alloc;
  gdouble ratio;
  gint64 duration;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));

  gtk_widget_get_allocation (GTK_WIDGET (self->ticks), &alloc);
  duration = sysprof_visualizer_get_duration (SYSPROF_VISUALIZER (self->ticks));

  if (alloc.width < 1)
    return 0;

  ratio = x / alloc.width;

  return self->begin_time + (ratio * duration);
}

static void
draw_selection_cb (SysprofSelection *selection,
                   gint64            range_begin,
                   gint64            range_end,
                   gpointer          user_data)
{
  SelectionDraw *draw = user_data;
  GdkRectangle area;
  gdouble x, x2;

  g_assert (SYSPROF_IS_SELECTION (selection));
  g_assert (draw != NULL);
  g_assert (draw->cr != NULL);
  g_assert (GTK_IS_LIST_BOX (draw->list));

  x = (range_begin - draw->begin_time) / (gdouble)draw->duration;
  x2 = (range_end - draw->begin_time) / (gdouble)draw->duration;

  area.x = x * draw->alloc.width;
  area.width = (x2 * draw->alloc.width) - area.x;
  area.y = 0;
  area.height = draw->alloc.height;

  if (area.width < 0)
    {
      area.width = ABS (area.width);
      area.x -= area.width;
    }

  gtk_render_background (draw->style_context, draw->cr, area.x + 2, area.y + 2, area.width - 4, area.height - 4);
}

static gboolean
visualizers_draw_after_cb (SysprofVisualizersFrame *self,
                           cairo_t                 *cr,
                           GtkListBox              *list)
{
  SelectionDraw draw;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (GTK_IS_LIST_BOX (list));

  draw.style_context = gtk_widget_get_style_context (GTK_WIDGET (list));
  draw.list = list;
  draw.cr = cr;
  draw.begin_time = self->begin_time;
  draw.duration = sysprof_visualizer_get_duration (SYSPROF_VISUALIZER (self->ticks));

  if (draw.duration == 0)
    return GDK_EVENT_PROPAGATE;

  gtk_widget_get_allocation (GTK_WIDGET (list), &draw.alloc);

  if (sysprof_selection_get_has_selection (self->selection) || self->button_pressed)
    {
      gtk_style_context_add_class (draw.style_context, "selection");
      sysprof_selection_foreach (self->selection, draw_selection_cb, &draw);
      if (self->button_pressed)
        draw_selection_cb (self->selection, self->drag_begin_at, self->drag_selection_at, &draw);
      gtk_style_context_remove_class (draw.style_context, "selection");
    }

  return GDK_EVENT_PROPAGATE;
}

static void
visualizers_realize_after_cb (SysprofVisualizersFrame *self,
                              GtkListBox              *list)
{
  GdkDisplay *display;
  GdkWindow *window;
  GdkCursor *cursor;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (GTK_IS_LIST_BOX (list));

  window = gtk_widget_get_window (GTK_WIDGET (list));
  display = gdk_window_get_display (window);
  cursor = gdk_cursor_new_from_name (display, "text");
  gdk_window_set_cursor (window, cursor);
  g_clear_object (&cursor);
}

static gboolean
visualizers_button_press_event_cb (SysprofVisualizersFrame *self,
                                   GdkEventButton          *ev,
                                   GtkListBox              *visualizers)
{
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (ev != NULL);
  g_assert (GTK_IS_LIST_BOX (visualizers));

  if (ev->button != GDK_BUTTON_PRIMARY)
    {
      if (sysprof_selection_get_has_selection (self->selection))
        {
          sysprof_selection_unselect_all (self->selection);
          return GDK_EVENT_STOP;
        }

      return GDK_EVENT_PROPAGATE;
    }

  if ((ev->state & GDK_SHIFT_MASK) == 0)
    sysprof_selection_unselect_all (self->selection);

  self->button_pressed = TRUE;

  self->drag_begin_at = get_time_from_x (self, ev->x);
  self->drag_selection_at = self->drag_begin_at;

  gtk_widget_queue_draw (GTK_WIDGET (visualizers));

  return GDK_EVENT_PROPAGATE;
}

static gboolean
visualizers_button_release_event_cb (SysprofVisualizersFrame *self,
                                     GdkEventButton          *ev,
                                     GtkListBox              *list)
{
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (ev != NULL);
  g_assert (GTK_IS_LIST_BOX (list));

  if (!self->button_pressed || ev->button != GDK_BUTTON_PRIMARY)
    return GDK_EVENT_PROPAGATE;

  self->button_pressed = FALSE;

  if (self->drag_begin_at != self->drag_selection_at)
    {
      sysprof_selection_select_range (self->selection,
                                      self->drag_begin_at,
                                      self->drag_selection_at);
      self->drag_begin_at = -1;
      self->drag_selection_at = -1;
    }

  gtk_widget_queue_draw (GTK_WIDGET (list));

  return GDK_EVENT_STOP;
}

static gboolean
visualizers_motion_notify_event_cb (SysprofVisualizersFrame *self,
                                    GdkEventMotion          *ev,
                                    GtkListBox              *list)
{
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (ev != NULL);
  g_assert (GTK_IS_LIST_BOX (list));

  if (!self->button_pressed)
    return GDK_EVENT_PROPAGATE;

  self->drag_selection_at = get_time_from_x (self, ev->x);

  gtk_widget_queue_draw (GTK_WIDGET (list));

  return GDK_EVENT_PROPAGATE;
}

static void
set_children_width_request_cb (GtkWidget *widget,
                               gpointer   data)
{
  gtk_widget_set_size_request (widget, GPOINTER_TO_INT (data), -1);
}

static void
set_children_width_request (GtkContainer *container,
                            gint          width)
{
  g_assert (GTK_IS_CONTAINER (container));

  gtk_container_foreach (container,
                         set_children_width_request_cb,
                         GINT_TO_POINTER (width));
}

static void
sysprof_visualizers_frame_notify_zoom (SysprofVisualizersFrame *self,
                                       GParamSpec              *pspec,
                                       SysprofZoomManager      *zoom_manager)
{
  gint64 duration;
  gint data_width;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (SYSPROF_IS_ZOOM_MANAGER (zoom_manager));

  duration = self->end_time - self->begin_time;
  data_width = sysprof_zoom_manager_get_width_for_duration (self->zoom_manager, duration);
  set_children_width_request (GTK_CONTAINER (self->ticks_viewport), data_width);
  set_children_width_request (GTK_CONTAINER (self->visualizers_viewport), data_width);
}

static gint
find_pos (SysprofVisualizersFrame *self,
          const gchar             *title,
          gint                     priority)
{
  GList *list;
  gint pos = 0;

  if (title == NULL)
    return -1;

  list = gtk_container_get_children (GTK_CONTAINER (self->visualizers));

  for (const GList *iter = list; iter; iter = iter->next)
    {
      SysprofVisualizerGroup *group = iter->data;
      gint prio = sysprof_visualizer_group_get_priority (group);
      const gchar *item = sysprof_visualizer_group_get_title (group);

      if (priority < prio ||
          (priority == prio && g_strcmp0 (title, item) < 0))
        break;

      pos++;
    }

  g_list_free (list);

  return pos;
}

static void
sysprof_visualizers_frame_add (GtkContainer *container,
                               GtkWidget    *child)
{
  SysprofVisualizersFrame *self = (SysprofVisualizersFrame *)container;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (GTK_IS_WIDGET (child));

  if (SYSPROF_IS_VISUALIZER_GROUP (child))
    {
      SysprofVisualizerGroupHeader *header;
      const gchar *title = sysprof_visualizer_group_get_title (SYSPROF_VISUALIZER_GROUP (child));
      gint priority = sysprof_visualizer_group_get_priority (SYSPROF_VISUALIZER_GROUP (child));
      gint pos = find_pos (self, title, priority);

      gtk_list_box_insert (self->visualizers, child, pos);

      header = _sysprof_visualizer_group_header_new ();
      g_object_set_data (G_OBJECT (header), "VISUALIZER_GROUP", child);
      gtk_list_box_insert (self->groups, GTK_WIDGET (header), pos);
      _sysprof_visualizer_group_set_header (SYSPROF_VISUALIZER_GROUP (child), header);
      gtk_widget_show (GTK_WIDGET (header));

      sysprof_visualizers_frame_notify_zoom (self, NULL, self->zoom_manager);

      return;
    }

  GTK_CONTAINER_CLASS (sysprof_visualizers_frame_parent_class)->add (container, child);
}

static void
sysprof_visualizers_frame_selection_changed (SysprofVisualizersFrame *self,
                                             SysprofSelection        *selection)
{
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (SYSPROF_IS_SELECTION (selection));

  gtk_widget_queue_draw (GTK_WIDGET (self->visualizers));
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SELECTION]);
}

static void
sysprof_visualizers_frame_group_activated_cb (SysprofVisualizersFrame      *self,
                                              SysprofVisualizerGroupHeader *row,
                                              GtkListBox                   *list)
{
  SysprofVisualizerGroup *group;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (SYSPROF_IS_VISUALIZER_GROUP_HEADER (row));

  group = g_object_get_data (G_OBJECT (row), "VISUALIZER_GROUP");
  g_assert (SYSPROF_IS_VISUALIZER_GROUP (group));

  g_signal_emit_by_name (group, "group-activated");
}

static void
sysprof_visualizers_frame_size_allocate (GtkWidget     *widget,
                                         GtkAllocation *alloc)
{
  SysprofVisualizersFrame *self = (SysprofVisualizersFrame *)widget;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_assert (alloc != NULL);

  sysprof_scrollmap_set_time_range (self->hscrollbar, self->begin_time, self->end_time);

  GTK_WIDGET_CLASS (sysprof_visualizers_frame_parent_class)->size_allocate (widget, alloc);
}

static void
sysprof_visualizers_frame_finalize (GObject *object)
{
  SysprofVisualizersFrame *self = (SysprofVisualizersFrame *)object;

  g_clear_object (&self->selection);

  G_OBJECT_CLASS (sysprof_visualizers_frame_parent_class)->finalize (object);
}

static void
sysprof_visualizers_frame_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofVisualizersFrame *self = SYSPROF_VISUALIZERS_FRAME (object);

  switch (prop_id)
    {
    case PROP_SELECTED_GROUP:
      g_value_set_object (value, sysprof_visualizers_frame_get_selected_group (self));
      break;

    case PROP_SELECTION:
      g_value_set_object (value, sysprof_visualizers_frame_get_selection (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizers_frame_class_init (SysprofVisualizersFrameClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->finalize = sysprof_visualizers_frame_finalize;
  object_class->get_property = sysprof_visualizers_frame_get_property;

  widget_class->size_allocate = sysprof_visualizers_frame_size_allocate;

  container_class->add = sysprof_visualizers_frame_add;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-visualizers-frame.ui");
  gtk_widget_class_set_css_name (widget_class, "SysprofVisualizersFrame");
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, groups);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, hscrollbar);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, hscroller);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, left_column);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, ticks);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, ticks_scroller);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, visualizers);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, vscroller);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, zoom_manager);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, zoom_scale);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, ticks_viewport);
  gtk_widget_class_bind_template_child (widget_class, SysprofVisualizersFrame, visualizers_viewport);

  properties [PROP_SELECTED_GROUP] =
    g_param_spec_object ("selected-group",
                         "Selected Group",
                         "The selected group",
                         SYSPROF_TYPE_VISUALIZER_GROUP,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTION] =
    g_param_spec_object ("selection",
                         "Selection",
                         "The time selection",
                         SYSPROF_TYPE_SELECTION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (SYSPROF_TYPE_SCROLLMAP);
  g_type_ensure (SYSPROF_TYPE_VISUALIZER_TICKS);
  g_type_ensure (SYSPROF_TYPE_ZOOM_MANAGER);
}

static void
sysprof_visualizers_frame_init (SysprofVisualizersFrame *self)
{
  GtkAdjustment *hadj;
  GtkAdjustment *zadj;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->selection = g_object_new (SYSPROF_TYPE_SELECTION, NULL);

  zadj = sysprof_zoom_manager_get_adjustment (self->zoom_manager);
  hadj = gtk_scrolled_window_get_hadjustment (self->hscroller);

  gtk_scrolled_window_set_hadjustment (self->ticks_scroller, hadj);
  gtk_range_set_adjustment (GTK_RANGE (self->hscrollbar), hadj);
  gtk_range_set_adjustment (GTK_RANGE (self->zoom_scale), zadj);

  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "zoom",
                                  G_ACTION_GROUP (self->zoom_manager));

  g_signal_connect_object (self->groups,
                           "row-activated",
                           G_CALLBACK (sysprof_visualizers_frame_group_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->selection,
                           "changed",
                           G_CALLBACK (sysprof_visualizers_frame_selection_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->visualizers,
                           "draw",
                           G_CALLBACK (visualizers_draw_after_cb),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  g_signal_connect_object (self->visualizers,
                           "realize",
                           G_CALLBACK (visualizers_realize_after_cb),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  g_signal_connect_object (self->visualizers,
                           "button-press-event",
                           G_CALLBACK (visualizers_button_press_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->visualizers,
                           "button-release-event",
                           G_CALLBACK (visualizers_button_release_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->visualizers,
                           "motion-notify-event",
                           G_CALLBACK (visualizers_motion_notify_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->zoom_manager,
                           "notify::zoom",
                           G_CALLBACK (sysprof_visualizers_frame_notify_zoom),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);
}

/**
 * sysprof_visualizers_frame_get_selected_group:
 *
 * Gets the currently selected group.
 *
 * Returns: (transfer none) (nullable): the selected row
 *
 * Since: 3.34
 */
SysprofVisualizerGroup *
sysprof_visualizers_frame_get_selected_group (SysprofVisualizersFrame *self)
{
  GtkListBoxRow *row;

  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), NULL);

  row = gtk_list_box_get_selected_row (self->groups);

  return SYSPROF_VISUALIZER_GROUP (row);
}

/**
 * sysprof_visualizers_frame_get_selection:
 *
 * Get the time selection
 *
 * Returns: (transfer none): a #SysprofSelection
 *
 * Since: 3.34
 */
SysprofSelection *
sysprof_visualizers_frame_get_selection (SysprofVisualizersFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), NULL);

  return self->selection;
}

static gint
compare_gint64 (const gint64 *a,
                const gint64 *b)
{
  if (*a < *b)
    return -1;
  else if (*a > *b)
    return 1;
  else
    return 0;
}

static bool
index_frame_times_frame_cb (const SysprofCaptureFrame *frame,
                            gpointer                   user_data)
{
  GArray *array = user_data;

  /* Track timing, but ignore some common types at startup */
  if (frame->type != SYSPROF_CAPTURE_FRAME_MAP &&
      frame->type != SYSPROF_CAPTURE_FRAME_PROCESS)
    g_array_append_val (array, frame->time);

  return TRUE;
}

static void
index_frame_times_worker (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
  SysprofCaptureCursor *cursor = task_data;
  GArray *timings = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (source_object));
  g_assert (cursor != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  timings = g_array_new (FALSE, FALSE, sizeof (gint64));
  sysprof_capture_cursor_foreach (cursor, index_frame_times_frame_cb, timings);
  g_array_sort (timings, (GCompareFunc) compare_gint64);

  g_task_return_pointer (task,
                         g_steal_pointer (&timings),
                         (GDestroyNotify) g_array_unref);
}

void
sysprof_visualizers_frame_load_async (SysprofVisualizersFrame *self,
                                      SysprofCaptureReader    *reader,
                                      GCancellable            *cancellable,
                                      GAsyncReadyCallback      callback,
                                      gpointer                 user_data)
{
  g_autoptr(GTask) task = NULL;
  GtkAllocation alloc;

  g_return_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  gtk_widget_get_allocation (GTK_WIDGET (self->ticks), &alloc);

  /* At this point, the SysprofDisplay should have already scanned the
   * reader for all of the events and therefore we can trust the begin
   * and end time for the capture.
   */
  self->begin_time = sysprof_capture_reader_get_start_time (reader);
  self->end_time = sysprof_capture_reader_get_end_time (reader);

   /* Now we need to run through the frames and index their times
    * so that we can calculate the number of items per bucket when
    * drawing the scrollbar.
    */
  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_visualizers_frame_load_async);
  g_task_set_task_data (task,
                        sysprof_capture_cursor_new (reader),
                        (GDestroyNotify) sysprof_capture_cursor_unref);
  g_task_run_in_thread (task, index_frame_times_worker);
}

gboolean
sysprof_visualizers_frame_load_finish (SysprofVisualizersFrame  *self,
                                       GAsyncResult             *result,
                                       GError                  **error)
{
  g_autoptr(GArray) timings = NULL;

  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((timings = g_task_propagate_pointer (G_TASK (result), error)))
    {
      sysprof_scrollmap_set_timings (self->hscrollbar, timings);
      sysprof_scrollmap_set_time_range (self->hscrollbar, self->begin_time, self->end_time);
      sysprof_visualizer_set_time_range (SYSPROF_VISUALIZER (self->ticks), self->begin_time, self->end_time);
      gtk_widget_queue_resize (GTK_WIDGET (self));

      return TRUE;
    }

  return FALSE;
}

/**
 * sysprof_visualizers_frame_get_zoom_manager:
 *
 * Gets the zoom manager for the frame.
 *
 * Returns: (transfer none): a #SysprofZoomManager
 */
SysprofZoomManager *
sysprof_visualizers_frame_get_zoom_manager (SysprofVisualizersFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), NULL);

  return self->zoom_manager;
}

/**
 * sysprof_visualizers_frame_get_size_group:
 *
 * gets the left column size group.
 *
 * Returns: (transfer none): a size group
 */
GtkSizeGroup *
sysprof_visualizers_frame_get_size_group (SysprofVisualizersFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), NULL);

  return self->left_column;
}

/**
 * sysprof_visualizers_frame_get_hadjustment:
 *
 * Gets the scroll adjustment used for horizontal scrolling
 *
 * Returns: (transfer none): a #GtkAdjustment
 */
GtkAdjustment *
sysprof_visualizers_frame_get_hadjustment (SysprofVisualizersFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self), NULL);

  return gtk_range_get_adjustment (GTK_RANGE (self->hscrollbar));
}

void
sysprof_visualizers_frame_unselect_row (SysprofVisualizersFrame *self)
{
  g_return_if_fail (SYSPROF_IS_VISUALIZERS_FRAME (self));

  gtk_list_box_unselect_all (self->groups);
}
