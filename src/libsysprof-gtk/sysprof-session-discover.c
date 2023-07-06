/* sysprof-session-discover.c
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

#include <glib/gi18n.h>

#include "sysprof-chart.h"
#include "sysprof-chart-layer.h"
#include "sysprof-color-iter-private.h"
#include "sysprof-column-layer.h"
#include "sysprof-line-layer.h"
#include "sysprof-session-private.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-track-private.h"
#include "sysprof-value-axis.h"

typedef enum _LineFlags
{
  LINE_FLAGS_DASHED                   = 1 << 0,
  LINE_FLAGS_NO_SPLINE                = 1 << 1,
  LINE_FLAGS_FILL                     = 1 << 2,
  LINE_FLAGS_IGNORE_RANGE_ON_SUBCHART = 1 << 3,
  LINE_FLAGS_FORMAT_MEMORY            = 1 << 4,
} LineFlags;

typedef struct _SysprofTrackSamples
{
  SysprofSession *session;
  GListModel *samples;
} SysprofTrackSamples;

typedef struct _SysprofTrackCounter
{
  const char *track_name;

  /* Used to layer values into main view */
  const char *category;
  const char *name;

  /* Used to split out sub views */
  const char *subtracks_category;
  const char *subtracks_name;

  double      min_value;
  double      max_value;

  LineFlags   flags;
} SysprofTrackCounter;

typedef struct _SysprofTrackCounterChart
{
  GListModel                *counters;
  GListModel                *subcounters;
  SysprofDocument           *document;
  SysprofSession            *session;
  const SysprofTrackCounter *info;
  GdkRGBA                    color;
  guint                      apply_range : 1;
} SysprofTrackCounterChart;

typedef struct _SysprofTrackMarksChart
{
  GListModel      *model;
  SysprofDocument *document;
  SysprofSession  *session;
} SysprofTrackMarksChart;

static const SysprofTrackCounter discovery_counters[] = {
  {
    N_("CPU Usage"),
    "CPU Percent", "Combined",
    "CPU Percent", "Total *",
    .0, 100.,
    LINE_FLAGS_FILL,
  },

  {
    N_("CPU Frequency"),
    "CPU Frequency", "*",
    "CPU Frequency", "*",
    .0, 100.,
    LINE_FLAGS_DASHED,
  },

  { N_("Memory"),
    "Memory", "Used",
    NULL, NULL,
    .0, .0,
    LINE_FLAGS_FILL | LINE_FLAGS_FORMAT_MEMORY,
  },

  { N_("FPS"), "gtk", "fps", "gtk", "*",
    0, 144, LINE_FLAGS_IGNORE_RANGE_ON_SUBCHART },

  {
    N_("Energy"),
    "RAPL", "*",
    "RAPL*", "*",
  },

};

static void
sysprof_track_samples_free (SysprofTrackSamples *samples)
{
  g_clear_object (&samples->samples);
  g_clear_weak_pointer (&samples->session);
  g_free (samples);
}

static SysprofTrackSamples *
sysprof_track_samples_new (SysprofSession *session,
                           GListModel     *samples)
{
  SysprofTrackSamples *state;

  state = g_new0 (SysprofTrackSamples, 1);
  state->samples = g_object_ref (samples);
  g_set_weak_pointer (&state->session, session);
  return state;
}

static void
sysprof_track_counter_chart_free (SysprofTrackCounterChart *info)
{
  g_clear_object (&info->counters);
  g_clear_object (&info->document);
  g_clear_weak_pointer (&info->session);
  info->info = NULL;
  g_free (info);
}

static void
sysprof_track_marks_chart_free (SysprofTrackMarksChart *info)
{
  g_clear_object (&info->model);
  g_clear_object (&info->document);
  g_clear_weak_pointer (&info->session);
  g_free (info);
}

static GListModel *
filter_counters (GListModel *model,
                 const char *category_glob,
                 const char *name_glob)
{
  g_autoptr(GListStore) store = NULL;
  g_autoptr(GPatternSpec) cat_spec = NULL;
  g_autoptr(GPatternSpec) name_spec = NULL;
  guint n_items;

  g_assert (G_IS_LIST_MODEL (model));

  if (category_glob == NULL || name_glob == NULL)
    return NULL;

  store = g_list_store_new (SYSPROF_TYPE_DOCUMENT_COUNTER);

  cat_spec = g_pattern_spec_new (category_glob);
  name_spec = g_pattern_spec_new (name_glob);

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (model, i);
      const char *ctrcat = sysprof_document_counter_get_category (counter);
      const char *ctrname = sysprof_document_counter_get_name (counter);

      if (g_pattern_spec_match (cat_spec, strlen (ctrcat), ctrcat, NULL) &&
          g_pattern_spec_match (name_spec, strlen (ctrname), ctrname, NULL))
        g_list_store_append (store, counter);
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (store)) == 0)
    return NULL;

  return G_LIST_MODEL (g_steal_pointer (&store));
}

static GtkWidget *
create_chart_for_samples (SysprofTrack        *track,
                          SysprofTrackSamples *state)
{
  g_autoptr(SysprofSeries) xy_series = NULL;
  g_autoptr(SysprofAxis) y_axis = NULL;
  SysprofChartLayer *layer;
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;

  g_assert (state != NULL);
  g_assert (SYSPROF_IS_SESSION (state->session));
  g_assert (SYSPROF_IS_TRACK (track));

  x_axis = sysprof_session_get_visible_time_axis (state->session);
  y_axis = sysprof_value_axis_new (0, 128);
  xy_series = sysprof_xy_series_new (sysprof_track_get_title (track),
                                     g_object_ref (state->samples),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_SAMPLE, NULL, "time"),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_SAMPLE, NULL, "stack-depth"));

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);
  layer = g_object_new (SYSPROF_TYPE_COLUMN_LAYER,
                        "series", xy_series,
                        "x-axis", x_axis,
                        "y-axis", y_axis,
                        NULL);
  sysprof_chart_add_layer (chart, layer);

  return GTK_WIDGET (chart);
}

static void
sysprof_session_discover_sampler (SysprofSession  *self,
                                  SysprofDocument *document,
                                  GListStore      *tracks)
{
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GListModel) samples_with_context_switch = NULL;
  g_autoptr(GListModel) samples_without_context_switch = NULL;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  samples = sysprof_document_list_samples (document);
  samples_with_context_switch = sysprof_document_list_samples_with_context_switch (document);
  samples_without_context_switch = sysprof_document_list_samples_without_context_switch (document);

  if (g_list_model_get_n_items (samples) > 0)
    {
      g_autoptr(SysprofTrack) track = NULL;

      track = g_object_new (SYSPROF_TYPE_TRACK,
                            "title", _("Profiler"),
                            NULL);
      g_signal_connect_data (track,
                             "create-chart",
                             G_CALLBACK (create_chart_for_samples),
                             sysprof_track_samples_new (self, samples),
                             (GClosureNotify)sysprof_track_samples_free,
                             0);
      g_list_store_append (tracks, track);

      if (g_list_model_get_n_items (samples_with_context_switch))
        {
          g_autoptr(SysprofTrack) subtrack = NULL;

          subtrack = g_object_new (SYSPROF_TYPE_TRACK,
                                   "title", _("Stack Traces (In Kernel)"),
                                   NULL);
          g_signal_connect_data (subtrack,
                                 "create-chart",
                                 G_CALLBACK (create_chart_for_samples),
                                 sysprof_track_samples_new (self, samples_with_context_switch),
                                 (GClosureNotify)sysprof_track_samples_free,
                                 0);
          _sysprof_track_add_subtrack (track, subtrack);
        }

      if (g_list_model_get_n_items (samples_without_context_switch))
        {
          g_autoptr(SysprofTrack) subtrack = NULL;

          subtrack = g_object_new (SYSPROF_TYPE_TRACK,
                                   "title", _("Stack Traces (In User)"),
                                   NULL);
          g_signal_connect_data (subtrack,
                                 "create-chart",
                                 G_CALLBACK (create_chart_for_samples),
                                 sysprof_track_samples_new (self, samples_without_context_switch),
                                 (GClosureNotify)sysprof_track_samples_free,
                                 0);
          _sysprof_track_add_subtrack (track, subtrack);
        }
    }
}

static GtkWidget *
create_chart_for_counters (SysprofTrack             *track,
                           SysprofTrackCounterChart *info)
{
  g_autoptr(SysprofDocumentCounter) first = NULL;
  g_autoptr(SysprofSeries) xy_series = NULL;
  g_autoptr(SysprofAxis) y_axis = NULL;
  SysprofChartLayer *layer;
  SysprofColorIter colors;
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;
  double min_value = 0;
  double max_value = 0;
  gboolean ignore_range = FALSE;
  guint n_items;

  g_assert (SYSPROF_IS_TRACK (track));
  g_assert (info != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (info->document));
  g_assert (SYSPROF_IS_SESSION (info->session));
  g_assert (G_IS_LIST_MODEL (info->counters));

  if (!(n_items = g_list_model_get_n_items (info->counters)))
    return NULL;

  sysprof_color_iter_init (&colors);

  x_axis = sysprof_session_get_visible_time_axis (info->session);

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);

  /* Setup Y axis usin first item. We'll expand the range
   * after we've processed all the counters.
   */
  first = g_list_model_get_item (info->counters, 0);
  min_value = sysprof_document_counter_get_min_value (first);
  max_value = sysprof_document_counter_get_max_value (first);
  y_axis = sysprof_value_axis_new (min_value, max_value);

  if (info->apply_range &&
      (info->info->min_value != .0 || info->info->max_value != .0))
    {
      min_value = info->info->min_value;
      max_value = info->info->max_value;
      ignore_range = TRUE;
    }

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (info->counters, i);
      double item_min_value = sysprof_document_counter_get_min_value (counter);
      double item_max_value = sysprof_document_counter_get_max_value (counter);
      const GdkRGBA *color;

      if (!ignore_range)
        {
          if (item_min_value < min_value)
            min_value = item_min_value;

          if (item_max_value > max_value)
            max_value = item_max_value;
        }

      if (info->color.alpha > 0)
        color = &info->color;
      else
        color = sysprof_color_iter_next (&colors);

      xy_series = sysprof_xy_series_new (sysprof_track_get_title (track),
                                         g_object_ref (G_LIST_MODEL (counter)),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "time"),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "value-double"));

      layer = g_object_new (SYSPROF_TYPE_LINE_LAYER,
                            "spline", !(info->info->flags & LINE_FLAGS_NO_SPLINE),
                            "dashed", !!(info->info->flags & LINE_FLAGS_DASHED),
                            "fill", !!(info->info->flags & LINE_FLAGS_FILL),
                            "color", color,
                            "series", xy_series,
                            "x-axis", x_axis,
                            "y-axis", y_axis,
                            NULL);
      sysprof_chart_add_layer (chart, layer);
    }

  sysprof_value_axis_set_min_value (SYSPROF_VALUE_AXIS (y_axis), min_value);
  sysprof_value_axis_set_max_value (SYSPROF_VALUE_AXIS (y_axis), max_value);

  return GTK_WIDGET (chart);
}

static char *
format_value_as_memory (SysprofTrack                *track,
                        SysprofDocumentCounterValue *value,
                        gpointer                     user_data)
{
  gint64 v = sysprof_document_counter_value_get_value_int64 (value);
  return g_format_size (v);
}

static void
sysprof_session_discover_counters (SysprofSession  *self,
                                   SysprofDocument *document,
                                   GListStore      *tracks)
{
  g_autoptr(GListModel) counters = NULL;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  counters = sysprof_document_list_counters (document);

  for (guint i = 0; i < G_N_ELEMENTS (discovery_counters); i++)
    {
      const SysprofTrackCounter *info = &discovery_counters[i];
      g_autoptr(GListModel) track_counters = filter_counters (counters, info->category, info->name);

      if (track_counters != NULL)
        {
          g_autoptr(SysprofTrack) track = NULL;
          g_autoptr(GListModel) subtrack_counters = NULL;
          SysprofTrackCounterChart *chart;
          SysprofColorIter iter;

          sysprof_color_iter_init (&iter);

          chart = g_new0 (SysprofTrackCounterChart, 1);
          g_set_weak_pointer (&chart->session, self);
          chart->document = g_object_ref (document);
          chart->counters = g_object_ref (track_counters);
          chart->info = info;
          chart->apply_range = TRUE;

          track = g_object_new (SYSPROF_TYPE_TRACK,
                                "title", info->track_name,
                                NULL);

          if ((info->flags & LINE_FLAGS_FORMAT_MEMORY) != 0)
            g_signal_connect (track,
                              "format-item-for-display",
                              G_CALLBACK (format_value_as_memory),
                              NULL);

          g_signal_connect_data (track,
                                 "create-chart",
                                 G_CALLBACK (create_chart_for_counters),
                                 chart,
                                 (GClosureNotify)sysprof_track_counter_chart_free,
                                 0);

          if ((subtrack_counters = filter_counters (counters, info->subtracks_category, info->subtracks_name)))
            {
              guint n_items = g_list_model_get_n_items (subtrack_counters);

              for (guint j = 0; j < n_items; j++)
                {
                  g_autoptr(SysprofDocumentCounter) subtrack_counter = g_list_model_get_item (subtrack_counters, j);
                  g_autoptr(SysprofTrack) subtrack = NULL;
                  g_autoptr(GListStore) store = g_list_store_new (SYSPROF_TYPE_DOCUMENT_COUNTER);
                  SysprofTrackCounterChart *subchart;

                  g_list_store_append (store, subtrack_counter);

                  subchart = g_new0 (SysprofTrackCounterChart, 1);
                  g_set_weak_pointer (&subchart->session, self);
                  subchart->document = g_object_ref (document);
                  subchart->counters = g_object_ref (G_LIST_MODEL (store));
                  subchart->info = info;
                  subchart->apply_range = !(info->flags & LINE_FLAGS_IGNORE_RANGE_ON_SUBCHART);
                  subchart->color = *sysprof_color_iter_next (&iter);

                  subtrack = g_object_new (SYSPROF_TYPE_TRACK,
                                           "title", sysprof_document_counter_get_name (subtrack_counter),
                                           NULL);

                  if ((info->flags & LINE_FLAGS_FORMAT_MEMORY) != 0)
                    g_signal_connect (subtrack,
                                      "format-item-for-display",
                                      G_CALLBACK (format_value_as_memory),
                                      NULL);

                  g_signal_connect_data (subtrack,
                                         "create-chart",
                                         G_CALLBACK (create_chart_for_counters),
                                         subchart,
                                         (GClosureNotify)sysprof_track_counter_chart_free,
                                         0);

                  _sysprof_track_add_subtrack (track, subtrack);
                }
            }

          g_list_store_append (tracks, track);
        }
    }
}

static gboolean
activate_mark_cb (SysprofSession       *session,
                  SysprofTimeSpanLayer *layer,
                  SysprofDocumentMark  *mark,
                  SysprofChart         *chart)
{
  gint64 duration;

  g_assert (SYSPROF_IS_SESSION (session));
  g_assert (SYSPROF_IS_TIME_SPAN_LAYER (layer));
  g_assert (SYSPROF_IS_DOCUMENT_MARK (mark));
  g_assert (SYSPROF_IS_CHART (chart));

  if ((duration = sysprof_document_mark_get_duration (mark)))
    {
      gint64 t = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      SysprofTimeSpan span = { t, t + duration };

      sysprof_session_select_time (session, &span);
      sysprof_session_zoom_to_selection (session);

      return TRUE;
    }

  return FALSE;
}

static GtkWidget *
create_chart_for_marks (SysprofTrack           *track,
                        SysprofTrackMarksChart *info)
{
  g_autoptr(SysprofSeries) time_series = NULL;
  SysprofChartLayer *layer;
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;

  g_assert (SYSPROF_IS_TRACK (track));
  g_assert (info != NULL);
  g_assert (SYSPROF_IS_SESSION (info->session));
  g_assert (SYSPROF_IS_DOCUMENT (info->document));
  g_assert (G_IS_LIST_MODEL (info->model));

  x_axis = sysprof_session_get_visible_time_axis (info->session);
  time_series = sysprof_time_series_new (sysprof_track_get_title (track),
                                         g_object_ref (G_LIST_MODEL (info->model)),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "time"),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "duration"),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "message"));

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);
  g_signal_connect_object (chart,
                           "activate-layer-item",
                           G_CALLBACK (activate_mark_cb),
                           info->session,
                           G_CONNECT_SWAPPED);
  layer = g_object_new (SYSPROF_TYPE_TIME_SPAN_LAYER,
                        "series", time_series,
                        "axis", x_axis,
                        NULL);
  sysprof_chart_add_layer (chart, layer);

  return GTK_WIDGET (chart);
}

static void
sysprof_session_discover_marks (SysprofSession  *self,
                                SysprofDocument *document,
                                GListStore      *tracks)
{
  g_autoptr(GListModel) catalogs = NULL;
  g_autoptr(SysprofTrack) toplevel = NULL;
  SysprofTrackMarksChart *topchart;
  guint n_catalogs;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  catalogs = sysprof_document_catalog_marks (document);
  n_catalogs = g_list_model_get_n_items (catalogs);

  if (n_catalogs == 0)
    return;

  toplevel = g_object_new (SYSPROF_TYPE_TRACK,
                           "title", _("Points of Interest"),
                           NULL);
  topchart = g_new0 (SysprofTrackMarksChart, 1);
  g_set_weak_pointer (&topchart->session, self);
  topchart->document = g_object_ref (document);
  topchart->model = sysprof_document_list_marks (document);
  g_signal_connect_data (toplevel,
                         "create-chart",
                         G_CALLBACK (create_chart_for_marks),
                         topchart,
                         (GClosureNotify)sysprof_track_marks_chart_free,
                         0);

  for (guint i = 0; i < n_catalogs; i++)
    {
      g_autoptr(GListModel) by_group = g_list_model_get_item (catalogs, i);
      g_autoptr(SysprofMarkCatalog) first = g_list_model_get_item (by_group, 0);
      g_autoptr(SysprofTrack) track = NULL;
      SysprofTrackMarksChart *chart;
      const char *group;
      guint n_names;

      if (first == NULL)
        continue;

      n_names = g_list_model_get_n_items (G_LIST_MODEL (by_group));
      group = sysprof_mark_catalog_get_group (first);

      chart = g_new0 (SysprofTrackMarksChart, 1);
      g_set_weak_pointer (&chart->session, self);
      chart->document = g_object_ref (document);
      chart->model = sysprof_document_list_marks_by_group (document, group);

      track = g_object_new (SYSPROF_TYPE_TRACK,
                            "title", sysprof_mark_catalog_get_group (first),
                            NULL);
      g_signal_connect_data (track,
                             "create-chart",
                             G_CALLBACK (create_chart_for_marks),
                             chart,
                             (GClosureNotify)sysprof_track_marks_chart_free,
                             0);
      _sysprof_track_add_subtrack (toplevel, track);

      for (guint j = 0; j < n_names; j++)
        {
          g_autoptr(SysprofMarkCatalog) catalog = g_list_model_get_item (by_group, j);
          g_autoptr(SysprofTrack) subtrack = NULL;
          SysprofTrackMarksChart *subchart;

          subchart = g_new0 (SysprofTrackMarksChart, 1);
          g_set_weak_pointer (&subchart->session, self);
          subchart->document = g_object_ref (document);
          subchart->model = g_object_ref (G_LIST_MODEL (catalog));

          subtrack = g_object_new (SYSPROF_TYPE_TRACK,
                                   "title", sysprof_mark_catalog_get_name (catalog),
                                   NULL);

          g_signal_connect_data (subtrack,
                                 "create-chart",
                                 G_CALLBACK (create_chart_for_marks),
                                 subchart,
                                 (GClosureNotify)sysprof_track_marks_chart_free,
                                 0);

          _sysprof_track_add_subtrack (track, subtrack);
        }
    }

  g_list_store_append (tracks, toplevel);
}

void
_sysprof_session_discover_tracks (SysprofSession  *self,
                                  SysprofDocument *document,
                                  GListStore      *tracks)
{
  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  sysprof_session_discover_sampler (self, document, tracks);
  sysprof_session_discover_counters (self, document, tracks);
  sysprof_session_discover_marks (self, document, tracks);
}
