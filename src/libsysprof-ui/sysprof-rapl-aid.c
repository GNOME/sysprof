/* sysprof-rapl-aid.c
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

#define G_LOG_DOMAIN "sysprof-rapl-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-rapl-aid.h"
#include "sysprof-line-visualizer.h"
#include "sysprof-proxy-aid.h"

struct _SysprofRaplAid
{
  SysprofProxyAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay       *display;
  GArray               *counters;
} Present;

G_DEFINE_TYPE (SysprofRaplAid, sysprof_rapl_aid, SYSPROF_TYPE_PROXY_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->cursor, sysprof_capture_cursor_unref);
  g_clear_pointer (&p->counters, g_array_unref);
  g_clear_object (&p->display);
  g_slice_free (Present, p);
}

/**
 * sysprof_rapl_aid_new:
 *
 * Create a new #SysprofRaplAid.
 *
 * Returns: (transfer full): a newly created #SysprofRaplAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_rapl_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_RAPL_AID, NULL);
}

static bool
collect_info (const SysprofCaptureFrame *frame,
              gpointer                   user_data)
{
  SysprofCaptureCounterDefine *def = (SysprofCaptureCounterDefine *)frame;
  Present *p = user_data;

  g_assert (frame != NULL);
  g_assert (p != NULL);
  g_assert (p->counters != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF)
    {
      for (guint i = 0; i < def->n_counters; i++)
        {
          const SysprofCaptureCounter *counter = &def->counters[i];

          if (g_str_has_prefix (counter->category, "RAPL"))
            g_array_append_vals (p->counters, counter, 1);
        }
    }

  return TRUE;
}

static void
sysprof_rapl_aid_present_worker (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
  Present *present = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_RAPL_AID (source_object));
  g_assert (present != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  sysprof_capture_cursor_foreach (present->cursor, collect_info, present);
  g_task_return_pointer (task,
                         g_steal_pointer (&present->counters),
                         (GDestroyNotify) g_array_unref);
}

static void
sysprof_rapl_aid_present_async (SysprofAid           *aid,
                               SysprofCaptureReader *reader,
                               SysprofDisplay       *display,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  static const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_CTRDEF, };
  g_autoptr(SysprofCaptureCondition) condition = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  Present present;

  g_assert (SYSPROF_IS_RAPL_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);
  present.counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_rapl_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_rapl_aid_present_worker);
}

static gboolean
sysprof_rapl_aid_present_finish (SysprofAid    *aid,
                                GAsyncResult  *result,
                                GError       **error)
{
  g_autoptr(GArray) counters = NULL;
  Present *present;

  g_assert (SYSPROF_IS_AID (aid));
  g_assert (G_IS_TASK (result));

  present = g_task_get_task_data (G_TASK (result));

  if ((counters = g_task_propagate_pointer (G_TASK (result), error)) && counters->len)
    {
      g_autoptr(SysprofColorCycle) cycle = sysprof_color_cycle_new ();
      g_autoptr(GHashTable) cat_to_row = g_hash_table_new (g_str_hash, g_str_equal);
      SysprofVisualizerGroup *energy;
      SysprofVisualizer *all;
      guint found = 0;

      energy = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                             "can-focus", TRUE,
                             "priority", -300,
                             "title", _("Energy Usage"),
                             "visible", TRUE,
                             NULL);

      all = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                          "title", _("Energy Usage (All)"),
                          "height-request", 35,
                          "visible", TRUE,
                          "y-lower", 0.0,
                          "units", "Watts",
                          NULL);
      sysprof_visualizer_group_insert (energy, SYSPROF_VISUALIZER (all), 0, FALSE);

      for (guint i = 0; i < counters->len; i++)
        {
          const SysprofCaptureCounter *ctr = &g_array_index (counters, SysprofCaptureCounter, i);

          /* The psuedo counters (core:-1 cpu:-1) have "RAPL" as the group */
          if (g_strcmp0 (ctr->category, "RAPL") == 0)
            {
              GdkRGBA rgba;

              sysprof_color_cycle_next (cycle, &rgba);
              sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (all), ctr->id, &rgba);
              found++;
            }
          else if (g_str_has_prefix (ctr->category, "RAPL "))
            {
              SysprofVisualizer *row;
              GdkRGBA rgba;

              row = g_hash_table_lookup (cat_to_row, ctr->category);

              if (row == NULL)
                {
                  row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                                      "title", ctr->category,
                                      "height-request", 20,
                                      "visible", FALSE,
                                      "y-lower", 0.0,
                                      "units", "Watts",
                                      NULL);
                  g_hash_table_insert (cat_to_row, (gchar *)ctr->category, row);
                  sysprof_visualizer_group_insert (energy, SYSPROF_VISUALIZER (row), -1, TRUE);
                }

              sysprof_color_cycle_next (cycle, &rgba);
              sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
              found++;
            }
        }

      if (found > 0)
        sysprof_display_add_group (present->display, energy);
      else
        gtk_widget_destroy (GTK_WIDGET (energy));
    }

  return counters != NULL;
}

static void
sysprof_rapl_aid_class_init (SysprofRaplAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->present_async = sysprof_rapl_aid_present_async;
  aid_class->present_finish = sysprof_rapl_aid_present_finish;
}

static void
sysprof_rapl_aid_init (SysprofRaplAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Energy Usage"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "battery-low-charging-symbolic");
  sysprof_proxy_aid_set_object_path (SYSPROF_PROXY_AID (self), "/org/gnome/Sysprof3/RAPL");
  sysprof_proxy_aid_set_bus_type (SYSPROF_PROXY_AID (self), G_BUS_TYPE_SYSTEM);
  sysprof_proxy_aid_set_bus_name (SYSPROF_PROXY_AID (self), "org.gnome.Sysprof3");
}
