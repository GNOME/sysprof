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
#include "sysprof-session-private.h"
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
} SysprofTrackCounter;

static const SysprofTrackCounter discovery_counters[] = {
  {
    N_("CPU"),
    "CPU Percent", "Combined",
    "CPU Percent", "*",
  },

  { N_("Memory"), "Memory", "Used", NULL, NULL },

  {
    N_("Energy"),
    "RAPL", "*",
    "RAPL *", "*",
  },

};

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

      if (g_pattern_spec_match (cat_spec, strlen (ctrcat), ctrcat, NULL) ||
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
  g_autoptr(GListModel) samples = NULL;
  SysprofChartLayer *layer;
  SysprofDocument *document;
  SysprofChart *chart;
  SysprofAxis *x_axis = NULL;

  g_assert (SYSPROF_IS_SESSION (session));
  g_assert (SYSPROF_IS_TRACK (track));

  document = sysprof_session_get_document (session);
  x_axis = sysprof_session_get_visible_time_axis (session);
  y_axis = sysprof_value_axis_new (0, 128);
  xy_series = sysprof_xy_series_new (_("Stack Depth"),
                                     sysprof_document_list_samples (document),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_SAMPLE, NULL, "time"),
                                     gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_SAMPLE, NULL, "stack-depth"));

  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "height-request", 48,
                        NULL);
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
          g_autoptr(GListModel) subtrack_counters = filter_counters (counters, info->subtracks_category, info->subtracks_name);

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
}
