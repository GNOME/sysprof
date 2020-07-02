/* sysprof-counters-aid.c
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

#define G_LOG_DOMAIN "sysprof-counters-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-counters-aid.h"
#include "sysprof-line-visualizer.h"
#include "sysprof-marks-page.h"
#include "sysprof-time-visualizer.h"

struct _SysprofCountersAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay       *display;
} Present;

G_DEFINE_TYPE (SysprofCountersAid, sysprof_counters_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->cursor, sysprof_capture_cursor_unref);
  g_clear_object (&p->display);
  g_slice_free (Present, p);
}

static void
on_group_activated_cb (SysprofVisualizerGroup *group,
                       SysprofPage            *page)
{
  SysprofDisplay *display;

  g_assert (SYSPROF_IS_VISUALIZER_GROUP (group));
  g_assert (SYSPROF_IS_PAGE (page));

  display = SYSPROF_DISPLAY (gtk_widget_get_ancestor (GTK_WIDGET (page), SYSPROF_TYPE_DISPLAY));
  sysprof_display_set_visible_page (display, page);
}

/**
 * sysprof_counters_aid_new:
 *
 * Create a new #SysprofCountersAid.
 *
 * Returns: (transfer full): a newly created #SysprofCountersAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_counters_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_COUNTERS_AID, NULL);
}

static void
sysprof_counters_aid_prepare (SysprofAid      *self,
                              SysprofProfiler *profiler)
{
}

static gchar *
build_title (const SysprofCaptureCounter *ctr)
{
  GString *str;

  str = g_string_new (NULL);

  if (ctr->category[0] != 0)
    {
      if (str->len)
        g_string_append_c (str, ' ');
      g_string_append (str, ctr->category);
    }

  if (ctr->name[0] != 0)
    {
      if (str->len)
        g_string_append (str, " — ");
      g_string_append (str, ctr->name);
    }

  if (ctr->description[0] != 0)
    {
      if (str->len)
        g_string_append_printf (str, " (%s)", ctr->description);
      else
        g_string_append (str, ctr->description);
    }

  if (str->len == 0)
    /* this is untranslated on purpose */
    g_string_append_printf (str, "Counter %d", ctr->id);

  return g_string_free (str, FALSE);
}

static bool
collect_counters (const SysprofCaptureFrame *frame,
                  gpointer                   user_data)
{
  SysprofCaptureCounterDefine *def = (SysprofCaptureCounterDefine *)frame;
  GArray *counters = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF);
  g_assert (counters != NULL);

  if (def->n_counters > 0)
    g_array_append_vals (counters, def->counters, def->n_counters);

  return TRUE;
}

static void
sysprof_counters_aid_present_worker (GTask        *task,
                                    gpointer      source_object,
                                    gpointer      task_data,
                                    GCancellable *cancellable)
{
  Present *present = task_data;
  g_autoptr(GArray) counters = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_COUNTERS_AID (source_object));
  g_assert (present != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));
  sysprof_capture_cursor_foreach (present->cursor, collect_counters, counters);
  g_task_return_pointer (task,
                         g_steal_pointer (&counters),
                         (GDestroyNotify) g_array_unref);
}

static void
sysprof_counters_aid_present_async (SysprofAid           *aid,
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

  g_assert (SYSPROF_IS_COUNTERS_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (1, types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_counters_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_counters_aid_present_worker);
}

static gboolean
sysprof_counters_aid_present_finish (SysprofAid    *aid,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  g_autoptr(GArray) counters = NULL;
  Present *present;

  g_assert (SYSPROF_IS_AID (aid));
  g_assert (G_IS_TASK (result));

  present = g_task_get_task_data (G_TASK (result));

  if ((counters = g_task_propagate_pointer (G_TASK (result), error)) && counters->len > 0)
    {
      g_autoptr(SysprofColorCycle) cycle = sysprof_color_cycle_new ();
      SysprofVisualizerGroup *group;
      SysprofVisualizer *combined;
      GtkWidget *page;

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "has-page", TRUE,
                            "title", _("Counters"),
                            "visible", TRUE,
                            NULL);

      combined = g_object_new (SYSPROF_TYPE_TIME_VISUALIZER,
                               "title", _("Counters"),
                               "height-request", 35,
                               "visible", TRUE,
                               NULL);
      sysprof_visualizer_group_insert (group, combined, -1, TRUE);

      for (guint i = 0; i < counters->len; i++)
        {
          const SysprofCaptureCounter *ctr = &g_array_index (counters, SysprofCaptureCounter, i);
          g_autofree gchar *title = build_title (ctr);
          GtkWidget *row;
          GdkRGBA rgba;

          row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                              "title", title,
                              "height-request", 35,
                              "visible", FALSE,
                              NULL);
          sysprof_color_cycle_next (cycle, &rgba);
          sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
          rgba.alpha = .5;
          sysprof_line_visualizer_set_fill (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
          sysprof_time_visualizer_add_counter (SYSPROF_TIME_VISUALIZER (combined), ctr->id, &rgba);
          sysprof_visualizer_group_insert (group, SYSPROF_VISUALIZER (row), -1, TRUE);
        }

      sysprof_display_add_group (present->display, group);

      page = sysprof_marks_page_new (sysprof_display_get_zoom_manager (present->display),
                                     SYSPROF_MARKS_MODEL_COUNTERS);
      gtk_widget_show (page);

      g_signal_connect_object (group,
                               "group-activated",
                               G_CALLBACK (on_group_activated_cb),
                               page,
                               0);
      sysprof_display_add_page (present->display, SYSPROF_PAGE (page));
    }

  return counters != NULL;
}

static void
sysprof_counters_aid_class_init (SysprofCountersAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_counters_aid_prepare;
  aid_class->present_async = sysprof_counters_aid_present_async;
  aid_class->present_finish = sysprof_counters_aid_present_finish;
}

static void
sysprof_counters_aid_init (SysprofCountersAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Battery"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof3-symbolic");
}
