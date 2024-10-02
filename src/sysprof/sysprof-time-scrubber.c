/* sysprof-time-scrubber.c
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

#include "sysprof-chart-layer.h"
#include "sysprof-session-private.h"
#include "sysprof-time-ruler.h"
#include "sysprof-time-scrubber.h"

struct _SysprofTimeScrubber
{
  GtkWidget         parent_instance;

  SysprofSession   *session;
  GSignalGroup     *session_signals;

  GtkLabel         *informative;
  GtkBox           *main;
  GtkLabel         *timecode;
  GtkBox           *vbox;
  SysprofTimeRuler *ruler;
  GtkButton        *zoom;

  double            motion_x;
  double            motion_y;

  double            drag_start_x;
  double            drag_start_y;
  double            drag_offset_x;
  double            drag_offset_y;

  guint             in_drag_selection : 1;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

static void buildable_iface_init (GtkBuildableIface *iface);
static GtkBuildableIface *buildable_parent;

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofTimeScrubber, sysprof_time_scrubber, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties[N_PROPS];

static void
set_motion (SysprofTimeScrubber *self,
            double               x,
            double               y)
{
  gboolean timecode_visible = FALSE;
  gboolean informative_visible = FALSE;
  GtkWidget *pick;
  int ruler_start;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  if (self->motion_x == x && self->motion_y == y)
    return;

  self->motion_x = x;
  self->motion_y = y;

  ruler_start = 0;
  timecode_visible = x >= ruler_start;

  if (timecode_visible)
    {
      g_autofree char *str = sysprof_time_ruler_get_label_at_point (self->ruler, x - ruler_start);
      gtk_label_set_label (self->timecode, str);
    }

  pick = gtk_widget_pick (GTK_WIDGET (self), x, y, 0);

  if (pick != NULL)
    {
      SysprofChartLayer *layer = SYSPROF_CHART_LAYER (gtk_widget_get_ancestor (pick, SYSPROF_TYPE_CHART_LAYER));

      if (layer != NULL)
        {
          g_autoptr(GObject) item = NULL;
          g_autofree char *text = NULL;
          graphene_point_t point;

          if (!gtk_widget_compute_point (GTK_WIDGET (self),
                                         GTK_WIDGET (layer),
                                         &GRAPHENE_POINT_INIT (x, y),
                                         &point))
            {
              if ((item = sysprof_chart_layer_lookup_item (layer, point.x, point.y)))
                text = _sysprof_session_describe (self->session, item);

              gtk_label_set_label (self->informative, text);

              informative_visible = text != NULL;
            }
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->timecode), timecode_visible);
  gtk_widget_set_visible (GTK_WIDGET (self->informative), informative_visible);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_time_scrubber_motion_enter_cb (SysprofTimeScrubber      *self,
                                       double                    x,
                                       double                    y,
                                       GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  set_motion (self, x, y);
}

static void
sysprof_time_scrubber_motion_leave_cb (SysprofTimeScrubber      *self,
                                       GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  set_motion (self, -1, -1);
}

static void
sysprof_time_scrubber_motion_cb (SysprofTimeScrubber      *self,
                                 double                    x,
                                 double                    y,
                                 GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  set_motion (self, x, y);
}

static void
sysprof_time_scrubber_drag_begin_cb (SysprofTimeScrubber *self,
                                     double               start_x,
                                     double               start_y,
                                     GtkGestureDrag      *drag)
{
  graphene_rect_t zoom_area;
  graphene_point_t point;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  if (!gtk_widget_compute_point (GTK_WIDGET (self->zoom),
                                 GTK_WIDGET (self),
                                 &GRAPHENE_POINT_INIT (0, 0),
                                 &point))
    return;

  zoom_area = GRAPHENE_RECT_INIT (point.x, point.y,
                                  gtk_widget_get_width (GTK_WIDGET (self->zoom)),
                                  gtk_widget_get_height (GTK_WIDGET (self->zoom)));

  if (start_x < 0 ||
      (gtk_widget_get_visible (GTK_WIDGET (self->zoom)) &&
       graphene_rect_contains_point (&zoom_area, &GRAPHENE_POINT_INIT (start_x, start_y))))
    {
      gtk_gesture_set_state (GTK_GESTURE (drag), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  self->drag_start_x = start_x;
  self->drag_start_y = start_y;
  self->drag_offset_x = 0;
  self->drag_offset_y = 0;

  gtk_widget_set_visible (GTK_WIDGET (self->zoom), FALSE);

  self->in_drag_selection = TRUE;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_time_scrubber_drag_end_cb (SysprofTimeScrubber *self,
                                   double               offset_x,
                                   double               offset_y,
                                   GtkGestureDrag      *drag)
{
  int base_x;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  if (self->session == NULL)
    goto cleanup;

  base_x = 0;

  if (self->drag_offset_x != .0)
    {
      graphene_rect_t selection;
      graphene_rect_t area;
      int width;
      int height;

      width = gtk_widget_get_width (GTK_WIDGET (self)) - base_x;
      height = gtk_widget_get_height (GTK_WIDGET (self));

      area = GRAPHENE_RECT_INIT (base_x, 0, width, height);
      selection = GRAPHENE_RECT_INIT (self->drag_start_x,
                                      0,
                                      self->drag_offset_x,
                                      height);
      graphene_rect_normalize (&selection);

      if (graphene_rect_intersection (&area, &selection, &selection))
        {
          double begin = (selection.origin.x - area.origin.x) / area.size.width;
          double end = (selection.origin.x + selection.size.width - area.origin.x) / area.size.width;
          const SysprofTimeSpan *visible = sysprof_session_get_visible_time (self->session);
          gint64 visible_duration = visible->end_nsec - visible->begin_nsec;
          SysprofTimeSpan to_select;

          to_select.begin_nsec = visible->begin_nsec + (begin * visible_duration);
          to_select.end_nsec = visible->begin_nsec + (end * visible_duration);

          sysprof_session_select_time (self->session, &to_select);

          gtk_widget_grab_focus (GTK_WIDGET (self->zoom));
        }
    }
  else if (self->drag_start_x >= base_x)
    {
      sysprof_session_select_time (self->session, sysprof_session_get_visible_time (self->session));
    }

cleanup:
  self->drag_start_x = -1;
  self->drag_start_y = -1;
  self->drag_offset_x = 0;
  self->drag_offset_y = 0;

  self->in_drag_selection = FALSE;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_time_scrubber_drag_update_cb (SysprofTimeScrubber *self,
                                      double               offset_x,
                                      double               offset_y,
                                      GtkGestureDrag      *drag)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  self->drag_offset_x = offset_x,
  self->drag_offset_y = offset_y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
get_selected_area (SysprofTimeScrubber *self,
                   graphene_rect_t   *area,
                   graphene_rect_t   *selection)
{
  const SysprofTimeSpan *selected;
  const SysprofTimeSpan *visible;
  SysprofTimeSpan relative;
  gint64 time_duration;
  double begin;
  double end;
  int base_x;
  int width;
  int height;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (area != NULL);
  g_assert (selection != NULL);

  if (self->session == NULL)
    return FALSE;

  base_x = 0;
  width = gtk_widget_get_width (GTK_WIDGET (self)) - base_x;
  height = gtk_widget_get_height (GTK_WIDGET (self));

  *area = GRAPHENE_RECT_INIT (base_x, 0, width, height);

  if (self->in_drag_selection && self->drag_offset_x != .0)
    {
      *selection = GRAPHENE_RECT_INIT (self->drag_start_x,
                                       0,
                                       self->drag_offset_x,
                                       height);
      graphene_rect_normalize (selection);
      return graphene_rect_intersection (area, selection, selection);
    }

  /* If selected range == visible range, then there is no selection */
  selected = sysprof_session_get_selected_time (self->session);
  visible = sysprof_session_get_visible_time (self->session);
  if (memcmp (selected, visible, sizeof *selected) == 0)
    return FALSE;

  time_duration = sysprof_time_span_duration (*visible);
  relative = sysprof_time_span_relative_to (*selected, visible->begin_nsec);

  begin = relative.begin_nsec / (double)time_duration;
  end = relative.end_nsec / (double)time_duration;

  *selection = GRAPHENE_RECT_INIT (area->origin.x + (begin * width),
                                   0,
                                   (end * width) - (begin * width),
                                   area->size.height);

  return TRUE;
}

static void
sysprof_time_scrubber_measure (GtkWidget      *widget,
                               GtkOrientation  orientation,
                               int             for_size,
                               int            *minimum,
                               int            *natural,
                               int            *minimum_baseline,
                               int            *natural_baseline)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)widget;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  gtk_widget_measure (GTK_WIDGET (self->main),
                      orientation,
                      for_size,
                      minimum,
                      natural,
                      minimum_baseline,
                      natural_baseline);
}

static void
sysprof_time_scrubber_size_allocate (GtkWidget *widget,
                                     int        width,
                                     int        height,
                                     int        baseline)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)widget;
  graphene_rect_t area;
  graphene_rect_t selection;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  gtk_widget_size_allocate (GTK_WIDGET (self->main),
                            &(GtkAllocation) {0, 0, width, height},
                            baseline);

  if (get_selected_area (self, &area, &selection))
    {
      graphene_point_t middle;
      GtkRequisition min_req;
      GtkRequisition nat_req;

      /* Position the zoom button in the center of the selected area */
      gtk_widget_get_preferred_size (GTK_WIDGET (self->zoom), &min_req, &nat_req);
      graphene_rect_get_center (&selection, &middle);
      gtk_widget_size_allocate (GTK_WIDGET (self->zoom),
                                &(GtkAllocation) {
                                  middle.x - (min_req.width/2),
                                  middle.y - (min_req.height/2),
                                  min_req.width,
                                  min_req.height
                                }, -1);
    }

  if (gtk_widget_get_visible (GTK_WIDGET (self->timecode)))
    {
      GtkRequisition min_req;
      GtkRequisition nat_req;

      gtk_widget_get_preferred_size (GTK_WIDGET (self->timecode), &min_req, &nat_req);

      if (self->motion_x + min_req.width < gtk_widget_get_width (GTK_WIDGET (self)))
        gtk_widget_size_allocate (GTK_WIDGET (self->timecode),
                                  &(GtkAllocation) {
                                    self->motion_x, 0,
                                    min_req.width, min_req.height
                                  }, -1);
      else
        gtk_widget_size_allocate (GTK_WIDGET (self->timecode),
                                  &(GtkAllocation) {
                                    self->motion_x - min_req.width, 0,
                                    min_req.width, min_req.height
                                  }, -1);
    }

  if (gtk_widget_get_visible (GTK_WIDGET (self->informative)))
    {
      GtkRequisition min_req;
      GtkRequisition nat_req;

      gtk_widget_get_preferred_size (GTK_WIDGET (self->informative), &min_req, &nat_req);

      if (self->motion_x + min_req.width < gtk_widget_get_width (GTK_WIDGET (self)))
        gtk_widget_size_allocate (GTK_WIDGET (self->informative),
                                  &(GtkAllocation) {
                                    self->motion_x, height - min_req.height,
                                    min_req.width, min_req.height
                                  }, -1);
      else
        gtk_widget_size_allocate (GTK_WIDGET (self->informative),
                                  &(GtkAllocation) {
                                    self->motion_x - min_req.width, height - min_req.height,
                                    min_req.width, min_req.height
                                  }, -1);
    }
}

static void
sysprof_time_scrubber_snapshot (GtkWidget   *widget,
                                GtkSnapshot *snapshot)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)widget;
  graphene_rect_t area;
  graphene_rect_t selection;
  GdkRGBA shadow_color;
  GdkRGBA line_color;
  GdkRGBA color;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->main), snapshot);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  {
    GtkStyleContext *style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
    gtk_style_context_get_color (style_context, &color);

    shadow_color = color;
    shadow_color.alpha *= .1;

    line_color = color;
    line_color.alpha *= .5;
  }
G_GNUC_END_IGNORE_DEPRECATIONS

  if (get_selected_area (self, &area, &selection))
    {
      gtk_snapshot_append_color (snapshot,
                                 &shadow_color,
                                 &GRAPHENE_RECT_INIT (area.origin.x,
                                                      area.origin.y,
                                                      selection.origin.x - area.origin.x,
                                                      area.size.height));

      gtk_snapshot_append_color (snapshot,
                                 &shadow_color,
                                 &GRAPHENE_RECT_INIT (selection.origin.x + selection.size.width,
                                                      area.origin.y,
                                                      (area.origin.x + area.size.width) - (selection.origin.x + selection.size.width),
                                                      area.size.height));
    }

  if (self->motion_x != -1 && self->motion_y != -1 && self->motion_x > 0)
    gtk_snapshot_append_color (snapshot,
                               &line_color,
                               &GRAPHENE_RECT_INIT (self->motion_x, 0, 1,
                                                    gtk_widget_get_height (GTK_WIDGET (self))));

  if (gtk_widget_get_visible (GTK_WIDGET (self->zoom)))
    gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->zoom), snapshot);

  if (gtk_widget_get_visible (GTK_WIDGET (self->timecode)))
    gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->timecode), snapshot);

  if (gtk_widget_get_visible (GTK_WIDGET (self->informative)))
    gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->informative), snapshot);
}

static void
notify_time_cb (SysprofTimeScrubber *self,
                GParamSpec          *pspec,
                SysprofSession      *session)
{
  const SysprofTimeSpan *visible;
  const SysprofTimeSpan *selected;
  gboolean button_visible;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (SYSPROF_IS_SESSION (session));

  visible = sysprof_session_get_visible_time (session);
  selected = sysprof_session_get_selected_time (session);

  button_visible = memcmp (visible, selected, sizeof *visible) != 0;

  gtk_widget_set_visible (GTK_WIDGET (self->zoom), button_visible);
}

static void
sysprof_time_scrubber_zoom_to_selection (GtkWidget  *widget,
                                         const char *action_name,
                                         GVariant   *params)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)widget;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  if (self->session != NULL)
    sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_time_scrubber_dispose (GObject *object)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TIME_SCRUBBER);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->session);
  g_clear_object (&self->session_signals);

  G_OBJECT_CLASS (sysprof_time_scrubber_parent_class)->dispose (object);
}

static void
sysprof_time_scrubber_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofTimeScrubber *self = SYSPROF_TIME_SCRUBBER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_time_scrubber_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_scrubber_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofTimeScrubber *self = SYSPROF_TIME_SCRUBBER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_time_scrubber_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_scrubber_class_init (SysprofTimeScrubberClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_time_scrubber_dispose;
  object_class->get_property = sysprof_time_scrubber_get_property;
  object_class->set_property = sysprof_time_scrubber_set_property;

  widget_class->measure = sysprof_time_scrubber_measure;
  widget_class->snapshot = sysprof_time_scrubber_snapshot;
  widget_class->size_allocate = sysprof_time_scrubber_size_allocate;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-time-scrubber.ui");

  gtk_widget_class_set_css_name (widget_class, "timescrubber");

  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, informative);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, main);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, ruler);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, timecode);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, vbox);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, zoom);

  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_motion_enter_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_motion_leave_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_motion_cb);

  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_drag_end_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_time_scrubber_drag_update_cb);

  gtk_widget_class_install_action (widget_class, "zoom-to-selection", NULL, sysprof_time_scrubber_zoom_to_selection);

  g_type_ensure (SYSPROF_TYPE_TIME_RULER);
}

static void
sysprof_time_scrubber_init (SysprofTimeScrubber *self)
{
  self->session_signals = g_signal_group_new (SYSPROF_TYPE_SESSION);
  g_signal_group_connect_object (self->session_signals,
                                 "notify::selected-time",
                                 G_CALLBACK (notify_time_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->session_signals,
                                 "notify::visible-time",
                                 G_CALLBACK (notify_time_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "text");
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self->zoom), "pointer");
}

SysprofSession *
sysprof_time_scrubber_get_session (SysprofTimeScrubber *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SCRUBBER (self), NULL);

  return self->session;
}

void
sysprof_time_scrubber_set_session (SysprofTimeScrubber *self,
                                   SysprofSession      *session)
{
  g_return_if_fail (SYSPROF_IS_TIME_SCRUBBER (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&self->session, session))
    {
      g_signal_group_set_target (self->session_signals, session);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
    }
}

void
sysprof_time_scrubber_add_chart (SysprofTimeScrubber *self,
                                 GtkWidget           *chart)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_WIDGET (chart));

  gtk_box_append (self->vbox, chart);
}

static void
sysprof_time_scrubber_add_child (GtkBuildable *buildable,
                                 GtkBuilder   *builder,
                                 GObject      *object,
                                 const char   *type)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)buildable;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  if (g_strcmp0 (type, "chart") == 0)
    sysprof_time_scrubber_add_chart (self, GTK_WIDGET (object));
  else
    buildable_parent->add_child (buildable, builder, object, type);
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  buildable_parent = g_type_interface_peek_parent (iface);
  iface->add_child = sysprof_time_scrubber_add_child;
}
