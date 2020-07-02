/* sysprof-callgraph-aid.c
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

#define G_LOG_DOMAIN "sysprof-callgraph-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-callgraph-aid.h"
#include "sysprof-callgraph-page.h"
#include "sysprof-depth-visualizer.h"

struct _SysprofCallgraphAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay *display;
  guint has_samples : 1;
} Present;

G_DEFINE_TYPE (SysprofCallgraphAid, sysprof_callgraph_aid, SYSPROF_TYPE_AID)

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
 * sysprof_callgraph_aid_new:
 *
 * Create a new #SysprofCallgraphAid.
 *
 * Returns: (transfer full): a newly created #SysprofCallgraphAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_callgraph_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_CALLGRAPH_AID, NULL);
}

static void
sysprof_callgraph_aid_prepare (SysprofAid      *self,
                               SysprofProfiler *profiler)
{
  g_assert (SYSPROF_IS_CALLGRAPH_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

#ifdef __linux__
  {
    const GPid *pids;
    guint n_pids;

    if ((pids = sysprof_profiler_get_pids (profiler, &n_pids)))
      {
        for (guint i = 0; i < n_pids; i++)
          {
            g_autoptr(SysprofSource) source = NULL;

            source = sysprof_perf_source_new ();
            sysprof_perf_source_set_target_pid (SYSPROF_PERF_SOURCE (source), pids[i]);
            sysprof_profiler_add_source (profiler, source);
          }
      }
    else
      {
        g_autoptr(SysprofSource) source = NULL;

        source = sysprof_perf_source_new ();
        sysprof_profiler_add_source (profiler, source);
      }
  }
#endif
}

static bool
discover_samples_cb (const SysprofCaptureFrame *frame,
                     gpointer                   user_data)
{
  Present *p = user_data;

  g_assert (frame != NULL);
  g_assert (p != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE)
    {
      p->has_samples = TRUE;
      return FALSE;
    }

  return TRUE;
}

static void
sysprof_callgraph_aid_present_worker (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  Present *p = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_CALLGRAPH_AID (source_object));
  g_assert (p != NULL);
  g_assert (p->cursor != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /* If we find a sample frame, then we should enable the callgraph
   * and stack visualizers.
   */
  sysprof_capture_cursor_foreach (p->cursor, discover_samples_cb, p);
  g_task_return_boolean (task, TRUE);
}

static void
sysprof_callgraph_aid_present_async (SysprofAid           *aid,
                                     SysprofCaptureReader *reader,
                                     SysprofDisplay       *display,
                                     GCancellable         *cancellable,
                                     GAsyncReadyCallback   callback,
                                     gpointer              user_data)
{
  static const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_SAMPLE };
  g_autoptr(SysprofCaptureCondition) condition = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  Present present;

  g_assert (SYSPROF_IS_CALLGRAPH_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (1, types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_callgraph_aid_present_worker);
}

static gboolean
sysprof_callgraph_aid_present_finish (SysprofAid    *aid,
                                      GAsyncResult  *result,
                                      GError       **error)
{
  Present *p;

  g_assert (SYSPROF_IS_CALLGRAPH_AID (aid));
  g_assert (G_IS_TASK (result));

  p = g_task_get_task_data (G_TASK (result));

  if (p->has_samples)
    {
      SysprofVisualizerGroup *group;
      SysprofVisualizer *depth;
      SysprofPage *page;

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "has-page", TRUE,
                            "priority", -500,
                            "title", _("Stack Traces"),
                            "visible", TRUE,
                            NULL);

      depth = sysprof_depth_visualizer_new (SYSPROF_DEPTH_VISUALIZER_COMBINED);
      g_object_set (depth,
                    "title", _("Stack Traces"),
                    "height-request", 35,
                    "visible", TRUE,
                    NULL);
      sysprof_visualizer_group_insert (group, depth, 0, FALSE);

      depth = sysprof_depth_visualizer_new (SYSPROF_DEPTH_VISUALIZER_KERNEL_ONLY);
      g_object_set (depth,
                    "title", _("Stack Traces (In Kernel)"),
                    "height-request", 35,
                    "visible", FALSE,
                    NULL);
      sysprof_visualizer_group_insert (group, depth, 1, TRUE);

      depth = sysprof_depth_visualizer_new (SYSPROF_DEPTH_VISUALIZER_USER_ONLY);
      g_object_set (depth,
                    "title", _("Stack Traces (In User)"),
                    "height-request", 35,
                    "visible", FALSE,
                    NULL);
      sysprof_visualizer_group_insert (group, depth, 2, TRUE);

      sysprof_display_add_group (p->display, group);

      page = g_object_new (SYSPROF_TYPE_CALLGRAPH_PAGE,
                           "title", _("Callgraph"),
                           "vexpand", TRUE,
                           "visible", TRUE,
                           NULL);
      sysprof_display_add_page (p->display, page);
      sysprof_display_set_visible_page (p->display, page);

      g_signal_connect_object (group,
                               "group-activated",
                               G_CALLBACK (on_group_activated_cb),
                               page,
                               0);
    }

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_callgraph_aid_class_init (SysprofCallgraphAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_callgraph_aid_prepare;
  aid_class->present_async = sysprof_callgraph_aid_present_async;
  aid_class->present_finish = sysprof_callgraph_aid_present_finish;
}

static void
sysprof_callgraph_aid_init (SysprofCallgraphAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Callgraph"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof-symbolic");
}
