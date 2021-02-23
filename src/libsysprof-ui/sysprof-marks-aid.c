/* sysprof-marks-aid.c
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

#define G_LOG_DOMAIN "sysprof-marks-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-marks-aid.h"
#include "sysprof-marks-page.h"
#include "sysprof-mark-visualizer.h"

struct _SysprofMarksAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofDisplay       *display;
  SysprofCaptureCursor *cursor;
  GHashTable           *categories;
  GHashTable           *kinds;
  guint                 last_kind;
  guint                 has_marks : 1;
} Present;

G_DEFINE_TYPE (SysprofMarksAid, sysprof_marks_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->categories, g_hash_table_unref);
  g_clear_pointer (&p->kinds, g_hash_table_unref);
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
 * sysprof_marks_aid_new:
 *
 * Create a new #SysprofMarksAid.
 *
 * Returns: (transfer full): a newly created #SysprofMarksAid
 */
SysprofAid *
sysprof_marks_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_MARKS_AID, NULL);
}

static bool
find_marks_cb (const SysprofCaptureFrame *frame,
               gpointer                   user_data)
{
  Present *p = user_data;

  g_assert (frame != NULL);
  g_assert (p != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_MARK)
    {
      const SysprofCaptureMark *mark = (const SysprofCaptureMark *)frame;
      SysprofMarkTimeSpan span = { frame->time, frame->time + mark->duration };
      gchar joined[64];
      gpointer kptr;
      GArray *items;

      p->has_marks = TRUE;

      if G_UNLIKELY (!(items = g_hash_table_lookup (p->categories, mark->group)))
        {
          items = g_array_new (FALSE, FALSE, sizeof (SysprofMarkTimeSpan));
          g_hash_table_insert (p->categories, g_strdup (mark->group), items);
        }

      g_snprintf (joined, sizeof joined, "%s:%s", mark->group, mark->name);

      if G_UNLIKELY (!(kptr = g_hash_table_lookup (p->kinds, joined)))
        {
          p->last_kind++;
          kptr = GINT_TO_POINTER (p->last_kind);
          g_hash_table_insert (p->kinds, g_strdup (joined), kptr);
        }

      span.kind = GPOINTER_TO_INT (kptr);

      g_array_append_val (items, span);
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
sysprof_marks_aid_present_worker (GTask        *task,
                                  gpointer      source_object,
                                  gpointer      task_data,
                                  GCancellable *cancellable)
{
  Present *p = task_data;
  GHashTableIter iter;
  gpointer k, v;

  g_assert (G_IS_TASK (task));
  g_assert (p != NULL);
  g_assert (SYSPROF_IS_DISPLAY (p->display));
  g_assert (p->cursor != NULL);
  g_assert (SYSPROF_IS_MARKS_AID (source_object));

  sysprof_capture_cursor_foreach (p->cursor, find_marks_cb, p);

  g_hash_table_iter_init (&iter, p->categories);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      GArray *spans = v;

      g_array_sort (spans, (GCompareFunc)compare_span);
    }

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_marks_aid_present_async (SysprofAid           *aid,
                                 SysprofCaptureReader *reader,
                                 SysprofDisplay       *display,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
  static const SysprofCaptureFrameType marks[] = {
    SYSPROF_CAPTURE_FRAME_MARK,
  };
  SysprofMarksAid *self = (SysprofMarksAid *)aid;
  g_autoptr(GTask) task = NULL;
  Present p = {0};

  g_assert (SYSPROF_IS_MARKS_AID (self));

  p.display = g_object_ref (display);
  p.categories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) g_array_unref);
  p.kinds = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  p.cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (p.cursor,
                                        sysprof_capture_condition_new_where_type_in (1, marks));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_marks_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &p),
                        present_free);
  g_task_run_in_thread (task, sysprof_marks_aid_present_worker);
}

static gboolean
sysprof_marks_aid_present_finish (SysprofAid    *aid,
                                  GAsyncResult  *result,
                                  GError       **error)
{
  Present *p;

  g_assert (SYSPROF_IS_MARKS_AID (aid));
  g_assert (G_IS_TASK (result));

  p = g_task_get_task_data (G_TASK (result));

  if (p->has_marks)
    {
      g_autoptr(SysprofColorCycle) cycle = sysprof_color_cycle_new ();
      SysprofVisualizerGroup *group;
      SysprofVisualizer *marks;
      SysprofPage *page;
      GHashTableIter iter;
      gpointer k, v;

      group = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "has-page", TRUE,
                            "title", _("Timings"),
                            "visible", TRUE,
                            NULL);

      marks = sysprof_mark_visualizer_new (p->categories);
      sysprof_visualizer_set_title (marks, _("Timings"));
      gtk_widget_show (GTK_WIDGET (marks));

      g_hash_table_iter_init (&iter, p->categories);
      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          g_autoptr(GHashTable) seen = g_hash_table_new_full (NULL, NULL, NULL, g_free);
          g_autoptr(GHashTable) scoped = NULL;
          SysprofVisualizer *scoped_vis;
          GArray *spans = v;
          const gchar *name = k;
          GdkRGBA rgba;
          GdkRGBA kind_rgba;
          gdouble ratio;

          sysprof_color_cycle_next (cycle, &rgba);
          sysprof_mark_visualizer_set_group_rgba (SYSPROF_MARK_VISUALIZER (marks), name, &rgba);

          /* Now make a scoped row just for this group */
          scoped = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                          (GDestroyNotify)g_array_unref);
          g_hash_table_insert (scoped, g_strdup (name), g_array_ref (spans));

          scoped_vis = sysprof_mark_visualizer_new (scoped);
          sysprof_visualizer_set_title (scoped_vis, name);
          sysprof_mark_visualizer_set_group_rgba (SYSPROF_MARK_VISUALIZER (scoped_vis), name, &rgba);
          sysprof_visualizer_group_insert (group, scoped_vis, -1, TRUE);

          ratio = .4 / p->last_kind;

          for (guint i = 0; i < spans->len; i++)
            {
              const SysprofMarkTimeSpan *span = &g_array_index (spans, SysprofMarkTimeSpan, i);

              if (!g_hash_table_contains (seen, GUINT_TO_POINTER (span->kind)))
                {
                  dzl_rgba_shade (&rgba, &kind_rgba, 1 + (ratio * span->kind));
                  g_hash_table_insert (seen,
                                       GUINT_TO_POINTER (span->kind),
                                       g_memdup2 (&kind_rgba, sizeof kind_rgba));
                }
            }

          sysprof_mark_visualizer_set_kind_rgba (SYSPROF_MARK_VISUALIZER (scoped_vis), seen);
        }

      page = g_object_new (SYSPROF_TYPE_MARKS_PAGE,
                           "zoom-manager", sysprof_display_get_zoom_manager (p->display),
                           "visible", TRUE,
                           NULL);

      g_signal_connect_object (group,
                               "group-activated",
                               G_CALLBACK (on_group_activated_cb),
                               page,
                               0);

      sysprof_visualizer_group_insert (group, marks, 0, FALSE);
      sysprof_display_add_group (p->display, group);
      sysprof_display_add_page (p->display, page);
    }

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_marks_aid_class_init (SysprofMarksAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->present_async = sysprof_marks_aid_present_async;
  aid_class->present_finish = sysprof_marks_aid_present_finish;
}

static void
sysprof_marks_aid_init (SysprofMarksAid *self)
{
}
