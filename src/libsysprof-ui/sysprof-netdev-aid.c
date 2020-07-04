/* sysprof-netdev-aid.c
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

#define G_LOG_DOMAIN "sysprof-netdev-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-duplex-visualizer.h"
#include "sysprof-netdev-aid.h"

struct _SysprofNetdevAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay       *display;
} Present;

G_DEFINE_TYPE (SysprofNetdevAid, sysprof_netdev_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->cursor, sysprof_capture_cursor_unref);
  g_clear_object (&p->display);
  g_slice_free (Present, p);
}

/**
 * sysprof_netdev_aid_new:
 *
 * Create a new #SysprofNetdevAid.
 *
 * Returns: (transfer full): a newly created #SysprofNetdevAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_netdev_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_NETDEV_AID, NULL);
}

static void
sysprof_netdev_aid_prepare (SysprofAid      *self,
                            SysprofProfiler *profiler)
{
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_NETDEV_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_netdev_source_new ();
  sysprof_profiler_add_source (profiler, source);
}

static bool
collect_netdev_counters (const SysprofCaptureFrame *frame,
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

      if (strcmp (counter->category, "Network") == 0 &&
          (g_str_has_prefix (counter->name, "RX Bytes") ||
           g_str_has_prefix (counter->name, "TX Bytes")))
        g_array_append_vals (counters, counter, 1);
    }

  return TRUE;
}

static void
sysprof_netdev_aid_present_worker (GTask        *task,
                                   gpointer      source_object,
                                   gpointer      task_data,
                                   GCancellable *cancellable)
{
  Present *present = task_data;
  g_autoptr(GArray) counters = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_NETDEV_AID (source_object));
  g_assert (present != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));
  sysprof_capture_cursor_foreach (present->cursor, collect_netdev_counters, counters);
  g_task_return_pointer (task,
                         g_steal_pointer (&counters),
                         (GDestroyNotify) g_array_unref);
}

static guint
find_other_id (GArray      *counters,
               const gchar *rx)
{
  g_autofree gchar *other = NULL;

  g_assert (counters);
  g_assert (rx != NULL);

  other = g_strdup (rx);
  other[0] = 'T';

  for (guint i = 0; i < counters->len; i++)
    {
      const SysprofCaptureCounter *c = &g_array_index (counters, SysprofCaptureCounter, i);

      if (g_str_equal (c->name, other))
        return c->id;
    }

  return 0;
}

static void
sysprof_netdev_aid_present_async (SysprofAid           *aid,
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

  g_assert (SYSPROF_IS_NETDEV_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (1, types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_netdev_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_netdev_aid_present_worker);
}

static gboolean
sysprof_netdev_aid_present_finish (SysprofAid    *aid,
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

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "title", _("Network"),
                            "visible", TRUE,
                            NULL);

      for (guint i = 0; i < counters->len; i++)
        {
          const SysprofCaptureCounter *ctr = &g_array_index (counters, SysprofCaptureCounter, i);

          if (g_str_has_prefix (ctr->name, "RX Bytes"))
            {
              g_autofree gchar *title = NULL;
              gboolean is_combined;
              GtkWidget *row;
              GdkRGBA rgba;
              guint other_id;

              if (!(other_id = find_other_id (counters, ctr->name)))
                continue;

              is_combined = g_str_equal (ctr->description, "Combined");

              if (is_combined)
                title = g_strdup ("Network Bytes (All)");
              else
                title = g_strdup_printf ("Network Bytes%s", ctr->name + strlen ("RX Bytes"));

              row = g_object_new (SYSPROF_TYPE_DUPLEX_VISUALIZER,
                                  "title", title,
                                  "height-request", 35,
                                  "visible", is_combined,
                                  NULL);
              sysprof_color_cycle_next (cycle, &rgba);
              sysprof_duplex_visualizer_set_counters  (SYSPROF_DUPLEX_VISUALIZER (row), ctr->id, other_id);
              sysprof_duplex_visualizer_set_colors (SYSPROF_DUPLEX_VISUALIZER (row), &rgba, &rgba);
              sysprof_visualizer_group_insert (group, SYSPROF_VISUALIZER (row), is_combined ? 0 : -1, !is_combined);
            }
        }

      if (counters->len > 0)
        sysprof_display_add_group (present->display, group);
      else
        gtk_widget_destroy (GTK_WIDGET (group));
    }

  return counters != NULL;
}

static void
sysprof_netdev_aid_class_init (SysprofNetdevAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_netdev_aid_prepare;
  aid_class->present_async = sysprof_netdev_aid_present_async;
  aid_class->present_finish = sysprof_netdev_aid_present_finish;
}

static void
sysprof_netdev_aid_init (SysprofNetdevAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Network"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "preferences-system-network-symbolic");
}
