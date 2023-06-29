/* sysprof-counter-track.c
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

#include "sysprof-counter-track-private.h"
#include "sysprof-chart.h"
#include "sysprof-line-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofCounterTrack
{
  SysprofTrack            parent_instance;
  SysprofDocumentCounter *counter;
};

G_DEFINE_FINAL_TYPE (SysprofCounterTrack, sysprof_counter_track, SYSPROF_TYPE_TRACK)

static GtkWidget *
sysprof_counter_track_create_chart (SysprofTrack *track)
{
  SysprofCounterTrack *self = (SysprofCounterTrack *)track;
  SysprofSession *session = NULL;
  g_autoptr(SysprofSeries) xy_series = NULL;
  g_autoptr(SysprofAxis) x_axis = NULL;
  g_autoptr(SysprofAxis) y_axis = NULL;
  SysprofChartLayer *layer;
  SysprofChart *chart;

  g_assert (SYSPROF_IS_COUNTER_TRACK (self));

  if (!(session = sysprof_track_get_session (track)))
    return NULL;

  x_axis = sysprof_session_get_visible_time_axis (session);
  y_axis = sysprof_value_axis_new (sysprof_document_counter_get_min_value (self->counter),
                                   sysprof_document_counter_get_max_value (self->counter));
  xy_series = sysprof_xy_series_new (sysprof_track_get_title (track),
                                     G_LIST_MODEL (self->counter),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "time"),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "value-double"));

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);
  layer = g_object_new (SYSPROF_TYPE_LINE_LAYER,
                        "spline", TRUE,
                        "series", xy_series,
                        "x-axis", x_axis,
                        "y-axis", y_axis,
                        NULL);
  sysprof_chart_add_layer (chart, layer);

  return GTK_WIDGET (chart);
}

static void
sysprof_counter_track_dispose (GObject *object)
{
  SysprofCounterTrack *self = (SysprofCounterTrack *)object;

  g_clear_object (&self->counter);

  G_OBJECT_CLASS (sysprof_counter_track_parent_class)->dispose (object);
}

static void
sysprof_counter_track_class_init (SysprofCounterTrackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofTrackClass *track_class = SYSPROF_TRACK_CLASS (klass);

  object_class->dispose = sysprof_counter_track_dispose;

  track_class->create_chart = sysprof_counter_track_create_chart;
}

static void
sysprof_counter_track_init (SysprofCounterTrack *self)
{
}

SysprofTrack *
sysprof_counter_track_new (SysprofSession         *session,
                           const char             *title,
                           SysprofDocumentCounter *counter)
{
  SysprofCounterTrack *self;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (counter), NULL);

  if (title == NULL)
    title = sysprof_document_counter_get_name (counter);

  self = g_object_new (SYSPROF_TYPE_COUNTER_TRACK,
                       "session", session,
                       "title", title,
                       NULL);

  self->counter = g_object_ref (counter);

  return SYSPROF_TRACK (self);
}
