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

#include "sysprof-counter-track-private.h"
#include "sysprof-session-private.h"
#include "sysprof-track.h"

typedef struct _SysprofTrackCounter
{
  const char *category;
  const char *name;
  const char *subtracks_name_glob;
  const char *track_name;
} SysprofTrackCounter;

static const SysprofTrackCounter discovery_counters[] = {
  { "CPU Percent", "Combined", "Total CPU *", N_("CPU Usage") },
};

static GListModel *
filter_counters (GListModel *model,
                 const char *category,
                 const char *name_glob)
{
  g_autoptr(GListStore) store = NULL;
  g_autoptr(GPatternSpec) spec = NULL;
  guint n_items;

  g_assert (G_IS_LIST_MODEL (model));
  g_assert (category != NULL);
  g_assert (name_glob != NULL);

  store = g_list_store_new (SYSPROF_TYPE_DOCUMENT_COUNTER);
  spec = g_pattern_spec_new (name_glob);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (model, i);
      const char *ctrcat = sysprof_document_counter_get_category (counter);
      const char *ctrname = sysprof_document_counter_get_name (counter);

      if (g_strcmp0 (category, ctrcat) == 0 &&
          g_pattern_spec_match (spec, strlen (ctrname), ctrname, NULL))
        g_list_store_append (store, counter);
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (store)) == 0)
    return NULL;

  return G_LIST_MODEL (g_steal_pointer (&store));
}

void
_sysprof_session_discover_tracks (SysprofSession  *self,
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
      g_autoptr(SysprofDocumentCounter) counter = NULL;

      if ((counter = sysprof_document_find_counter (document, info->category, info->name)))
        {
          g_autoptr(SysprofTrack) track = NULL;
          g_autoptr(GListModel) subcounters = NULL;

          track = sysprof_counter_track_new (self,
                                             g_dgettext (GETTEXT_PACKAGE, info->track_name),
                                             counter);

          if ((subcounters = filter_counters (counters, info->category, info->subtracks_name_glob)))
            {
            }

          g_list_store_append (tracks, track);
        }
    }
}
