/* sysprof-battery-aid.c
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

#define G_LOG_DOMAIN "sysprof-battery-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-battery-aid.h"
#include "sysprof-line-visualizer.h"

struct _SysprofBatteryAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay       *display;
} Present;

G_DEFINE_TYPE (SysprofBatteryAid, sysprof_battery_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->cursor, sysprof_capture_cursor_unref);
  g_clear_object (&p->display);
  g_slice_free (Present, p);
}

/**
 * sysprof_battery_aid_new:
 *
 * Create a new #SysprofBatteryAid.
 *
 * Returns: (transfer full): a newly created #SysprofBatteryAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_battery_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_BATTERY_AID, NULL);
}

static void
sysprof_battery_aid_prepare (SysprofAid      *self,
                             SysprofProfiler *profiler)
{
#ifdef __linux__
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_BATTERY_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_battery_source_new ();
  sysprof_profiler_add_source (profiler, source);
#endif
}

static bool
collect_battery_counters (const SysprofCaptureFrame *frame,
                          gpointer                   user_data)
{
  SysprofCaptureCounterDefine *def = (SysprofCaptureCounterDefine *)frame;
  GArray *counters = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF);
  g_assert (counters != NULL);

  for (guint i = 0; i < def->n_counters; i++)
    {
      const SysprofCaptureCounter *counter = &def->counters[i];

      if (g_strcmp0 (counter->category, "Battery Charge") == 0)
        g_array_append_vals (counters, counter, 1);
    }

  return TRUE;
}

static void
sysprof_battery_aid_present_worker (GTask        *task,
                                    gpointer      source_object,
                                    gpointer      task_data,
                                    GCancellable *cancellable)
{
  Present *present = task_data;
  g_autoptr(GArray) counters = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_BATTERY_AID (source_object));
  g_assert (present != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));
  sysprof_capture_cursor_foreach (present->cursor, collect_battery_counters, counters);
  g_task_return_pointer (task,
                         g_steal_pointer (&counters),
                         (GDestroyNotify) g_array_unref);
}

static void
sysprof_battery_aid_present_async (SysprofAid           *aid,
                                   SysprofCaptureReader *reader,
                                   SysprofDisplay       *display,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data)
{
  static const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_CTRDEF };
  g_autoptr(SysprofCaptureCondition) condition = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  Present present;

  g_assert (SYSPROF_IS_BATTERY_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (1, types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_battery_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_battery_aid_present_worker);
}

static gboolean
sysprof_battery_aid_present_finish (SysprofAid    *aid,
                                    GAsyncResult  *result,
                                    GError       **error)
{
  g_autoptr(GArray) counters = NULL;
  Present *present;

  g_assert (SYSPROF_IS_AID (aid));
  g_assert (G_IS_TASK (result));

  present = g_task_get_task_data (G_TASK (result));

  if ((counters = g_task_propagate_pointer (G_TASK (result), error)))
    {
      g_autoptr(SysprofColorCycle) cycle = sysprof_color_cycle_new ();
      SysprofVisualizerGroup *group;
      guint found = 0;

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "title", _("Battery Charge"),
                            "visible", TRUE,
                            NULL);

      for (guint i = 0; i < counters->len; i++)
        {
          const SysprofCaptureCounter *ctr = &g_array_index (counters, SysprofCaptureCounter, i);

          if (g_strcmp0 (ctr->category, "Battery Charge") == 0)
            {
              g_autofree gchar *title = NULL;
              gboolean is_combined = g_str_equal (ctr->name, "Combined");
              GtkWidget *row;
              GdkRGBA rgba;

              if (is_combined)
                title = g_strdup (_("Battery Charge (All)"));
              else
                title = g_strdup_printf ("Battery Charge (%s)", ctr->name);

              row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                                  "title", title,
                                  "height-request", 35,
                                  "visible", is_combined,
                                  NULL);
              sysprof_color_cycle_next (cycle, &rgba);
              sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
              sysprof_visualizer_group_insert (group,
                                               SYSPROF_VISUALIZER (row),
                                               is_combined ? 0 : -1,
                                               !is_combined);

              found++;
            }
        }

      if (found > 0)
        sysprof_display_add_group (present->display, group);
      else
        gtk_widget_destroy (GTK_WIDGET (group));
    }

  return counters != NULL;
}

static void
sysprof_battery_aid_class_init (SysprofBatteryAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_battery_aid_prepare;
  aid_class->present_async = sysprof_battery_aid_present_async;
  aid_class->present_finish = sysprof_battery_aid_present_finish;
}

static void
sysprof_battery_aid_init (SysprofBatteryAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Battery"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "battery-low-charging-symbolic");
}
