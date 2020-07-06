/* sysprof-details-page.c
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

#define G_LOG_DOMAIN "sysprof-details-page"

#include "config.h"

#include <dazzle.h>
#include <glib/gi18n.h>
#include <string.h>

#include "sysprof-details-page.h"
#include "sysprof-ui-private.h"

struct _SysprofDetailsPage
{
  SysprofPage   parent_instance;

  /* Template Objects */
  DzlThreeGrid *three_grid;
  GtkListStore *marks_store;
  GtkTreeView  *marks_view;
  GtkLabel     *counters;
  GtkLabel     *duration;
  GtkLabel     *filename;
  GtkLabel     *allocations;
  GtkLabel     *forks;
  GtkLabel     *marks;
  GtkLabel     *processes;
  GtkLabel     *samples;
  GtkLabel     *start_time;
  GtkLabel     *cpu_label;

  guint         next_row;
};

G_DEFINE_TYPE (SysprofDetailsPage, sysprof_details_page, GTK_TYPE_BIN)

#if GLIB_CHECK_VERSION(2, 56, 0)
# define _g_date_time_new_from_iso8601 g_date_time_new_from_iso8601
#else
static GDateTime *
_g_date_time_new_from_iso8601 (const gchar *str,
                               GTimeZone   *default_tz)
{
  GTimeVal tv;

  if (g_time_val_from_iso8601 (str, &tv))
    {
      g_autoptr(GDateTime) dt = g_date_time_new_from_timeval_utc (&tv);

      if (default_tz)
        return g_date_time_to_timezone (dt, default_tz);
      else
        return g_steal_pointer (&dt);
    }

  return NULL;
}
#endif

static void
sysprof_details_page_class_init (SysprofDetailsPageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-details-page.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, allocations);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, counters);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, cpu_label);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, duration);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, filename);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, forks);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, marks);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, marks_store);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, marks_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, processes);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, samples);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, start_time);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsPage, three_grid);

  g_type_ensure (DZL_TYPE_THREE_GRID);
}

static void
sysprof_details_page_init (SysprofDetailsPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (self->marks_view),
                               GTK_SELECTION_MULTIPLE);

  self->next_row = 8;
}

GtkWidget *
sysprof_details_page_new (void)
{
  return g_object_new (SYSPROF_TYPE_DETAILS_PAGE, NULL);
}

static void
update_cpu_info_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(SysprofDetailsPage) self = user_data;
  g_autofree gchar *str = NULL;

  g_assert (SYSPROF_IS_DETAILS_PAGE (self));
  g_assert (G_IS_TASK (result));

  if ((str = g_task_propagate_pointer (G_TASK (result), NULL)))
    gtk_label_set_label (self->cpu_label, str);
}

static bool
cpu_info_cb (const SysprofCaptureFrame *frame,
             gpointer                   user_data)
{
  const SysprofCaptureFileChunk *fc = (gpointer)frame;
  const gchar *endptr;
  const gchar *line;
  gchar **str = user_data;

  line = memmem ((gchar *)fc->data, fc->len, "model name", 10);

  if (!line)
    return FALSE;

  endptr = (gchar *)fc->data + fc->len;
  endptr = memchr (line, '\n', endptr - line);

  if (endptr)
    {
      gchar *tmp = *str = g_strndup (line, endptr - line);
      for (; *tmp && *tmp != ':'; tmp++)
        *tmp = ' ';
      if (*tmp == ':')
        *tmp = ' ';
      g_strstrip (*str);
      return FALSE;
    }

  return TRUE;
}

static void
sysprof_details_page_update_cpu_info_worker (GTask        *task,
                                             gpointer      source_object,
                                             gpointer      task_data,
                                             GCancellable *cancellable)
{
  SysprofCaptureCursor *cursor = task_data;
  gchar *str = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (cursor != NULL);

  sysprof_capture_cursor_foreach (cursor, cpu_info_cb, &str);
  g_task_return_pointer (task, g_steal_pointer (&str), g_free);
}

static void
sysprof_details_page_update_cpu_info (SysprofDetailsPage   *self,
                                      SysprofCaptureReader *reader)
{
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_DETAILS_PAGE (self));
  g_assert (reader != NULL);

  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor,
                                        sysprof_capture_condition_new_where_file ("/proc/cpuinfo"));

  task = g_task_new (NULL, NULL, update_cpu_info_cb, g_object_ref (self));
  g_task_set_task_data (task,
                        g_steal_pointer (&cursor),
                        (GDestroyNotify) sysprof_capture_cursor_unref);
  g_task_run_in_thread (task, sysprof_details_page_update_cpu_info_worker);
}

void
sysprof_details_page_set_reader (SysprofDetailsPage   *self,
                                 SysprofCaptureReader *reader)
{
  g_autoptr(GDateTime) dt = NULL;
  g_autoptr(GDateTime) local = NULL;
  g_autofree gchar *duration_str = NULL;
  const gchar *filename;
  const gchar *capture_at;
  SysprofCaptureStat st_buf;
  gint64 duration;

  g_return_if_fail (SYSPROF_IS_DETAILS_PAGE (self));
  g_return_if_fail (reader != NULL);

  sysprof_details_page_update_cpu_info (self, reader);

  if (!(filename = sysprof_capture_reader_get_filename (reader)))
    filename = _("Memory Capture");

  gtk_label_set_label (self->filename, filename);

  if ((capture_at = sysprof_capture_reader_get_time (reader)) &&
      (dt = _g_date_time_new_from_iso8601 (capture_at, NULL)) &&
      (local = g_date_time_to_local (dt)))
    {
      g_autofree gchar *str = g_date_time_format (local, "%x %X");
      gtk_label_set_label (self->start_time, str);
    }

  duration = sysprof_capture_reader_get_end_time (reader) -
             sysprof_capture_reader_get_start_time (reader);
  duration_str = g_strdup_printf (_("%0.4lf seconds"), duration / (gdouble)SYSPROF_NSEC_PER_SEC);
  gtk_label_set_label (self->duration, duration_str);

  if (sysprof_capture_reader_get_stat (reader, &st_buf))
    {
#define SET_FRAME_COUNT(field, TYPE) \
      G_STMT_START { \
        g_autofree gchar *str = NULL; \
        str = g_strdup_printf ("%"G_GSIZE_FORMAT, st_buf.frame_count[TYPE]); \
        gtk_label_set_label (self->field, str); \
      } G_STMT_END

      SET_FRAME_COUNT (samples, SYSPROF_CAPTURE_FRAME_SAMPLE);
      SET_FRAME_COUNT (marks, SYSPROF_CAPTURE_FRAME_MARK);
      SET_FRAME_COUNT (processes, SYSPROF_CAPTURE_FRAME_PROCESS);
      SET_FRAME_COUNT (forks, SYSPROF_CAPTURE_FRAME_FORK);
      SET_FRAME_COUNT (counters, SYSPROF_CAPTURE_FRAME_CTRSET);
      SET_FRAME_COUNT (allocations, SYSPROF_CAPTURE_FRAME_ALLOCATION);

#undef SET_FRAME_COUNT
    }
}

void
sysprof_details_page_add_item (SysprofDetailsPage *self,
                               GtkWidget          *left,
                               GtkWidget          *center)
{
  g_return_if_fail (SYSPROF_IS_DETAILS_PAGE (self));
  g_return_if_fail (!left || GTK_IS_WIDGET (left));
  g_return_if_fail (!center || GTK_IS_WIDGET (center));

  if (left)
    gtk_container_add_with_properties (GTK_CONTAINER (self->three_grid), left,
                                       "row", self->next_row,
                                       "column", DZL_THREE_GRID_COLUMN_LEFT,
                                       NULL);

  if (center)
    gtk_container_add_with_properties (GTK_CONTAINER (self->three_grid), center,
                                       "row", self->next_row,
                                       "column", DZL_THREE_GRID_COLUMN_CENTER,
                                       NULL);

  self->next_row++;
}

void
sysprof_details_page_add_mark (SysprofDetailsPage *self,
                               const gchar        *mark,
                               gint64              min,
                               gint64              max,
                               gint64              avg,
                               gint64              hits)
{
  GtkTreeIter iter;

  g_return_if_fail (SYSPROF_IS_DETAILS_PAGE (self));

  gtk_list_store_append (self->marks_store, &iter);
  gtk_list_store_set (self->marks_store, &iter,
                      0, mark,
                      1, min ? _sysprof_format_duration (min) : "—",
                      2, max ? _sysprof_format_duration (max) : "—",
                      3, avg ? _sysprof_format_duration (avg) : "—",
                      4, hits,
                      -1);
}

void
sysprof_details_page_add_marks  (SysprofDetailsPage    *self,
                                 const SysprofMarkStat *marks,
                                 guint                  n_marks)
{
  g_return_if_fail (SYSPROF_IS_DETAILS_PAGE (self));
  g_return_if_fail (marks != NULL || n_marks == 0);

  if (marks == NULL || n_marks == 0)
    return;

  /* Be reasonable */
  if (n_marks > 100)
    n_marks = 100;

  for (guint i = 0; i < n_marks; i++)
    sysprof_details_page_add_mark (self,
                                   marks[i].name,
                                   marks[i].min,
                                   marks[i].max,
                                   marks[i].avg,
                                   marks[i].count);
}
