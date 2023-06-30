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
#include "sysprof-column-layer.h"
#include "sysprof-line-layer.h"
#include "sysprof-session-private.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-track-private.h"
#include "sysprof-value-axis.h"

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
} SysprofTrackCounter;

typedef struct _SysprofTrackCounterChart
{
  GListModel                *counters;
  GListModel                *subcounters;
  SysprofDocument           *document;
  SysprofSession            *session;
  const SysprofTrackCounter *info;
} SysprofTrackCounterChart;

typedef struct _SysprofTrackMarksChart
{
  GListModel      *model;
  SysprofDocument *document;
  SysprofSession  *session;
} SysprofTrackMarksChart;

static const SysprofTrackCounter discovery_counters[] = {
  {
    N_("CPU"),
    "CPU Percent", "Combined",
    "CPU Percent", "Total *",
    .0, 100.,
  },

  { N_("Memory"), "Memory", "Used", NULL, NULL },

  {
    N_("Energy"),
    "RAPL", "*",
    "RAPL*", "*",
  },

};

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
create_chart_for_samples (SysprofSession *session,
                          SysprofTrack   *track)
{
  g_autoptr(SysprofSeries) xy_series = NULL;
  g_autoptr(SysprofAxis) y_axis = NULL;
  SysprofChartLayer *layer;
  SysprofDocument *document;
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;

  g_assert (SYSPROF_IS_SESSION (session));
  g_assert (SYSPROF_IS_TRACK (track));

  document = sysprof_session_get_document (session);
  x_axis = sysprof_session_get_visible_time_axis (session);
  y_axis = sysprof_value_axis_new (0, 128);
  xy_series = sysprof_xy_series_new (sysprof_track_get_title (track),
                                     sysprof_document_list_samples (document),
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

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  samples = sysprof_document_list_samples (document);

  if (g_list_model_get_n_items (samples) > 0)
    {
      g_autoptr(SysprofTrack) track = NULL;

      track = g_object_new (SYSPROF_TYPE_TRACK,
                            "title", _("Profiler"),
                            NULL);
      g_signal_connect_object (track,
                               "create-chart",
                               G_CALLBACK (create_chart_for_samples),
                               self,
                               G_CONNECT_SWAPPED);
      g_list_store_append (tracks, track);
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
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;
  double min_value = 0;
  double max_value = 0;
  guint n_items;

  g_assert (SYSPROF_IS_TRACK (track));
  g_assert (info != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (info->document));
  g_assert (SYSPROF_IS_SESSION (info->session));
  g_assert (G_IS_LIST_MODEL (info->counters));

  if (!(n_items = g_list_model_get_n_items (info->counters)))
    return NULL;

  x_axis = sysprof_session_get_visible_time_axis (info->session);

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);

  /* Setup Y axis usin first item. We'll expand the range
   * after we've processed all the counters.
   */
  first = g_list_model_get_item (info->counters, 0);
  min_value = sysprof_document_counter_get_min_value (first);
  max_value = sysprof_document_counter_get_max_value (first);
  y_axis = sysprof_value_axis_new (min_value, max_value);

  if (info->info->min_value != .0 || info->info->max_value != .0)
    {
      min_value = info->info->min_value;
      max_value = info->info->max_value;
    }

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (info->counters, i);
      double item_min_value = sysprof_document_counter_get_min_value (counter);
      double item_max_value = sysprof_document_counter_get_max_value (counter);

      if (item_min_value < min_value)
        min_value = item_min_value;

      if (item_max_value > max_value)
        max_value = item_max_value;

      xy_series = sysprof_xy_series_new (sysprof_track_get_title (track),
                                         g_object_ref (G_LIST_MODEL (counter)),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "time"),
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "value-double"));

      layer = g_object_new (SYSPROF_TYPE_LINE_LAYER,
                            "spline", TRUE,
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

          chart = g_new0 (SysprofTrackCounterChart, 1);
          g_set_weak_pointer (&chart->session, self);
          chart->document = g_object_ref (document);
          chart->counters = g_object_ref (track_counters);
          chart->info = info;

          track = g_object_new (SYSPROF_TYPE_TRACK,
                                "title", info->track_name,
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

                  subtrack = g_object_new (SYSPROF_TYPE_TRACK,
                                           "title", sysprof_document_counter_get_name (subtrack_counter),
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
                                         gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "duration"));

  chart = g_object_new (SYSPROF_TYPE_CHART, NULL);
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
  guint n_catalogs;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  catalogs = sysprof_document_catalog_marks (document);
  n_catalogs = g_list_model_get_n_items (catalogs);

  if (n_catalogs == 0)
    return;

  for (guint i = 0; i < n_catalogs; i++)
    {
      g_autoptr(GListModel) by_group = g_list_model_get_item (catalogs, i);
      g_autoptr(SysprofMarkCatalog) first = g_list_model_get_item (by_group, 0);
      g_autoptr(SysprofTrack) track = NULL;
      SysprofTrackMarksChart *chart;
      guint n_names = g_list_model_get_n_items (G_LIST_MODEL (by_group));

      if (first == NULL)
        continue;

      chart = g_new0 (SysprofTrackMarksChart, 1);
      g_set_weak_pointer (&chart->session, self);
      chart->document = g_object_ref (document);
      chart->model = G_LIST_MODEL (gtk_flatten_list_model_new (G_LIST_MODEL (by_group)));

      track = g_object_new (SYSPROF_TYPE_TRACK,
                            "title", sysprof_mark_catalog_get_group (first),
                            NULL);
      g_signal_connect_data (track,
                             "create-chart",
                             G_CALLBACK (create_chart_for_marks),
                             chart,
                             (GClosureNotify)sysprof_track_marks_chart_free,
                             0);
      g_list_store_append (tracks, track);

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
