/* sysprof-tracks-view.c
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

#include "sysprof-css-private.h"
#include "sysprof-track-view.h"
#include "sysprof-tracks-view.h"
#include "sysprof-time-ruler.h"

struct _SysprofTracksView
{
  GtkWidget       parent_instance;

  SysprofSession *session;

  GtkBox         *box;
  GtkWidget      *top_left;
  GtkListView    *list_view;
  GtkButton      *zoom;

  double          motion_x;
  double          motion_y;

  double          drag_start_x;
  double          drag_start_y;
  double          drag_offset_x;
  double          drag_offset_y;

  guint           in_drag_selection : 1;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTracksView, sysprof_tracks_view, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_tracks_view_motion_enter_cb (SysprofTracksView        *self,
                                     double                    x,
                                     double                    y,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = x;
  self->motion_y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_tracks_view_motion_leave_cb (SysprofTracksView        *self,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = -1;
  self->motion_y = -1;

  gtk_widget_queue_draw (GTK_WIDGET (self));

}

static void
sysprof_tracks_view_motion_cb (SysprofTracksView        *self,
                               double                    x,
                               double                    y,
                               GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = x;
  self->motion_y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_tracks_view_drag_begin_cb (SysprofTracksView *self,
                                   double             start_x,
                                   double             start_y,
                                   GtkGestureDrag    *drag)
{
  graphene_rect_t zoom_area;
  double x, y;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  gtk_widget_translate_coordinates (GTK_WIDGET (self->zoom),
                                    GTK_WIDGET (self),
                                    0, 0, &x, &y);
  zoom_area = GRAPHENE_RECT_INIT (x, y,
                                  gtk_widget_get_width (GTK_WIDGET (self->zoom)),
                                  gtk_widget_get_height (GTK_WIDGET (self->zoom)));

  if (start_x < gtk_widget_get_width (self->top_left) ||
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
sysprof_tracks_view_drag_end_cb (SysprofTracksView *self,
                                 double             offset_x,
                                 double             offset_y,
                                 GtkGestureDrag    *drag)
{
  int base_x;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  if (self->session == NULL)
    goto cleanup;

  base_x = gtk_widget_get_width (self->top_left);

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
        }
    }
  else if (self->drag_start_x >= base_x)
    {
      sysprof_session_select_time (self->session, NULL);
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
sysprof_tracks_view_drag_update_cb (SysprofTracksView *self,
                                    double             offset_x,
                                    double             offset_y,
                                    GtkGestureDrag    *drag)
{
  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  self->drag_offset_x = offset_x,
  self->drag_offset_y = offset_y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
get_selected_area (SysprofTracksView *self,
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

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (area != NULL);
  g_assert (selection != NULL);

  if (self->session == NULL)
    return FALSE;

  base_x = gtk_widget_get_width (self->top_left);
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
sysprof_tracks_view_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  SysprofTracksView *self = (SysprofTracksView *)widget;
  graphene_rect_t area;
  graphene_rect_t selection;
  GdkRGBA shadow_color;
  GdkRGBA line_color;
  GdkRGBA color;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->box), snapshot);

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

  if (self->motion_x != -1 &&
      self->motion_y != -1 &&
      self->motion_x > gtk_widget_get_width (self->top_left))
    gtk_snapshot_append_color (snapshot,
                               &line_color,
                               &GRAPHENE_RECT_INIT (self->motion_x, 0, 1,
                                                    gtk_widget_get_height (GTK_WIDGET (self))));

  if (gtk_widget_get_visible (GTK_WIDGET (self->zoom)))
    gtk_widget_snapshot_child (GTK_WIDGET (self), GTK_WIDGET (self->zoom), snapshot);
}

static void
sysprof_tracks_view_zoom_to_selection (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *params)
{
  SysprofTracksView *self = (SysprofTracksView *)widget;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));

  if (self->session != NULL)
    sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_tracks_view_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  SysprofTracksView *self = (SysprofTracksView *)widget;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));

  gtk_widget_measure (GTK_WIDGET (self->box),
                      orientation,
                      for_size,
                      minimum,
                      natural,
                      minimum_baseline,
                      natural_baseline);
}

static void
sysprof_tracks_view_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
  SysprofTracksView *self = (SysprofTracksView *)widget;
  graphene_rect_t area;
  graphene_rect_t selection;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));

  gtk_widget_size_allocate (GTK_WIDGET (self->box),
                            &(GtkAllocation) {0, 0, width, height},
                            baseline);

  if (get_selected_area (self, &area, &selection))
    {
      graphene_point_t middle;
      GtkRequisition min_req;
      GtkRequisition nat_req;

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
}

static void
sysprof_tracks_view_dispose (GObject *object)
{
  SysprofTracksView *self = (SysprofTracksView *)object;
  GtkWidget *child;

  sysprof_tracks_view_set_session (self, NULL);

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TRACKS_VIEW);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_tracks_view_parent_class)->dispose (object);
}

static void
sysprof_tracks_view_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofTracksView *self = SYSPROF_TRACKS_VIEW (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_tracks_view_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracks_view_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofTracksView *self = SYSPROF_TRACKS_VIEW (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_tracks_view_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracks_view_class_init (SysprofTracksViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_tracks_view_dispose;
  object_class->get_property = sysprof_tracks_view_get_property;
  object_class->set_property = sysprof_tracks_view_set_property;

  widget_class->snapshot = sysprof_tracks_view_snapshot;
  widget_class->measure = sysprof_tracks_view_measure;
  widget_class->size_allocate = sysprof_tracks_view_size_allocate;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/libsysprof-gtk/sysprof-tracks-view.ui");
  gtk_widget_class_set_css_name (widget_class, "tracks");

  gtk_widget_class_bind_template_child (widget_class, SysprofTracksView, box);
  gtk_widget_class_bind_template_child (widget_class, SysprofTracksView, list_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofTracksView, top_left);
  gtk_widget_class_bind_template_child (widget_class, SysprofTracksView, zoom);

  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_motion_enter_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_motion_leave_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_motion_cb);

  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_drag_end_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_tracks_view_drag_update_cb);

  gtk_widget_class_install_action (widget_class, "zoom-to-selection", NULL, sysprof_tracks_view_zoom_to_selection);

  g_type_ensure (SYSPROF_TYPE_TIME_RULER);
  g_type_ensure (SYSPROF_TYPE_TRACK_VIEW);
}

static void
sysprof_tracks_view_init (SysprofTracksView *self)
{
  _sysprof_css_init ();

  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_tracks_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_TRACKS_VIEW, NULL);
}

/**
 * sysprof_tracks_view_get_session:
 * @self: a #SysprofTracksView
 *
 * Gets the session for the tracks.
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_tracks_view_get_session (SysprofTracksView *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACKS_VIEW (self), NULL);

  return self->session;
}

static GListModel *
sysprof_tracks_view_create_model_func (gpointer item,
                                       gpointer user_data)
{
  g_assert (SYSPROF_IS_TRACK (item));

  return sysprof_track_list_subtracks (item);
}

static void
sysprof_tracks_view_notify_time_cb (SysprofTracksView *self,
                                    GParamSpec        *pspec,
                                    SysprofSession    *session)
{
  const SysprofTimeSpan *visible;
  const SysprofTimeSpan *selected;
  gboolean button_visible;

  g_assert (SYSPROF_IS_TRACKS_VIEW (self));
  g_assert (SYSPROF_IS_SESSION (session));

  visible = sysprof_session_get_visible_time (session);
  selected = sysprof_session_get_selected_time (session);

  button_visible = memcmp (visible, selected, sizeof *visible) != 0;

  gtk_widget_set_visible (GTK_WIDGET (self->zoom), button_visible);
}

void
sysprof_tracks_view_set_session (SysprofTracksView *self,
                                 SysprofSession    *session)
{
  g_return_if_fail (SYSPROF_IS_TRACKS_VIEW (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (self->session == session)
    return;

  if (self->session)
    {
       g_signal_handlers_disconnect_by_func (self->session,
                                             G_CALLBACK (sysprof_tracks_view_notify_time_cb),
                                             self);
      gtk_list_view_set_model (self->list_view, NULL);
      g_clear_object (&self->session);
    }

  if (session)
    {
      g_autoptr(GtkTreeListModel) tree_list_model = NULL;
      g_autoptr(GtkNoSelection) no = NULL;
      g_autoptr(GListModel) tracks = NULL;

      self->session = g_object_ref (session);

      tracks = sysprof_session_list_tracks (session);
      tree_list_model = gtk_tree_list_model_new (g_object_ref (tracks),
                                                 FALSE,
                                                 FALSE,
                                                 sysprof_tracks_view_create_model_func,
                                                 self, NULL);
      no = gtk_no_selection_new (g_object_ref (G_LIST_MODEL (tree_list_model)));

      gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (no));

      g_signal_connect_object (session,
                               "notify::selected-time",
                               G_CALLBACK (sysprof_tracks_view_notify_time_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (session,
                               "notify::visible-time",
                               G_CALLBACK (sysprof_tracks_view_notify_time_cb),
                               self,
                               G_CONNECT_SWAPPED);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}
