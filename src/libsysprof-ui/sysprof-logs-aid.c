/* sysprof-logs-aid.c
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

#define G_LOG_DOMAIN "sysprof-logs-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-logs-aid.h"
#include "sysprof-logs-page.h"
#include "sysprof-mark-visualizer.h"

struct _SysprofLogsAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofDisplay       *display;
  SysprofCaptureCursor *cursor;
  GArray               *log_marks;
} Present;

G_DEFINE_TYPE (SysprofLogsAid, sysprof_logs_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->log_marks, g_array_unref);
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
 * sysprof_logs_aid_new:
 *
 * Create a new #SysprofLogsAid.
 *
 * Returns: (transfer full): a newly created #SysprofLogsAid
 */
SysprofAid *
sysprof_logs_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_LOGS_AID, NULL);
}

static bool
find_marks_cb (const SysprofCaptureFrame *frame,
               gpointer                   user_data)
{
  Present *p = user_data;

  g_assert (frame != NULL);
  g_assert (p != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_LOG)
    {
      SysprofMarkTimeSpan span = { frame->time, frame->time };
      g_array_append_val (p->log_marks, span);
    }

  return TRUE;
}

static gint
compare_span (const SysprofMarkTimeSpan *a,
              const SysprofMarkTimeSpan *b)
{
  if (a->kind < b->kind)
    return -1;

  if (b->kind < a->kind)
    return 1;

  if (a->begin < b->begin)
    return -1;

  if (b->begin < a->begin)
    return 1;

  if (b->end > a->end)
    return -1;

  return 0;
}

static void
sysprof_logs_aid_present_worker (GTask        *task,
                                  gpointer      source_object,
                                  gpointer      task_data,
                                  GCancellable *cancellable)
{
  Present *p = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (p != NULL);
  g_assert (SYSPROF_IS_DISPLAY (p->display));
  g_assert (p->cursor != NULL);
  g_assert (SYSPROF_IS_LOGS_AID (source_object));

  sysprof_capture_cursor_foreach (p->cursor, find_marks_cb, p);
  g_array_sort (p->log_marks, (GCompareFunc)compare_span);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_logs_aid_present_async (SysprofAid           *aid,
                                SysprofCaptureReader *reader,
                                SysprofDisplay       *display,
                                GCancellable         *cancellable,
                                GAsyncReadyCallback   callback,
                                gpointer              user_data)
{
  static const SysprofCaptureFrameType logs[] = {
    SYSPROF_CAPTURE_FRAME_LOG,
  };
  SysprofLogsAid *self = (SysprofLogsAid *)aid;
  g_autoptr(GTask) task = NULL;
  Present p = {0};

  g_assert (SYSPROF_IS_LOGS_AID (self));

  p.display = g_object_ref (display);
  p.log_marks = g_array_new (FALSE, FALSE, sizeof (SysprofMarkTimeSpan));
  p.cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (p.cursor,
                                        sysprof_capture_condition_new_where_type_in (1, logs));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_logs_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &p),
                        present_free);
  g_task_run_in_thread (task, sysprof_logs_aid_present_worker);
}

static gboolean
sysprof_logs_aid_present_finish (SysprofAid    *aid,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  Present *p;

  g_assert (SYSPROF_IS_LOGS_AID (aid));
  g_assert (G_IS_TASK (result));

  p = g_task_get_task_data (G_TASK (result));

  if (p->log_marks->len > 0)
    {
      g_autoptr(GHashTable) items = NULL;
      SysprofVisualizerGroup *group;
      SysprofVisualizer *marks;
      SysprofPage *page;

      items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                     (GDestroyNotify) g_array_unref);
      g_hash_table_insert (items, g_strdup (_("Logs")), g_array_ref (p->log_marks));

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "title", _("Logs"),
                            "visible", TRUE,
                            NULL);

      marks = sysprof_mark_visualizer_new (items);
      sysprof_visualizer_set_title (marks, _("Logs"));
      gtk_widget_show (GTK_WIDGET (marks));
      sysprof_visualizer_group_insert (group, marks, 0, FALSE);
      sysprof_display_add_group (p->display, group);

      page = g_object_new (SYSPROF_TYPE_LOGS_PAGE,
                           "title", _("Logs"),
                           "visible", TRUE,
                           NULL);
      sysprof_display_add_page (p->display, page);

      g_signal_connect_object (group,
                               "group-activated",
                               G_CALLBACK (on_group_activated_cb),
                               page,
                               0);
    }

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_logs_aid_class_init (SysprofLogsAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->present_async = sysprof_logs_aid_present_async;
  aid_class->present_finish = sysprof_logs_aid_present_finish;
}

static void
sysprof_logs_aid_init (SysprofLogsAid *self)
{
}
