/* sysprof-details-view.c
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

#define G_LOG_DOMAIN "sysprof-details-view"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-details-view.h"

#define NSEC_PER_SEC (G_USEC_PER_SEC * 1000L)

struct _SysprofDetailsView
{
  GtkBin    parent_instance;

  /* Template Objects */
  GtkBox *left_box;
  GtkBox *center_box;
  GtkLabel *duration;
  GtkLabel *filename;
  GtkLabel *forks;
  GtkLabel *marks;
  GtkLabel *processes;
  GtkLabel *samples;
  GtkLabel *start_time;
  GtkBox   *vbox;
};

G_DEFINE_TYPE (SysprofDetailsView, sysprof_details_view, GTK_TYPE_BIN)

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
sysprof_details_view_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_details_view_parent_class)->finalize (object);
}

static void
sysprof_details_view_class_init (SysprofDetailsViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_details_view_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-details-view.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, duration);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, filename);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, forks);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, marks);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, processes);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, samples);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, start_time);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, vbox);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, left_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofDetailsView, center_box);
}

static void
sysprof_details_view_init (SysprofDetailsView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_details_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_DETAILS_VIEW, NULL);
}

void
sysprof_details_view_set_reader (SysprofDetailsView   *self,
                                 SysprofCaptureReader *reader)
{
  g_autoptr(GDateTime) dt = NULL;
  g_autoptr(GDateTime) local = NULL;
  g_autofree gchar *duration_str = NULL;
  const gchar *filename;
  const gchar *capture_at;
  SysprofCaptureStat st_buf;
  gint64 duration;

  g_return_if_fail (SYSPROF_IS_DETAILS_VIEW (self));
  g_return_if_fail (reader != NULL);

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
  duration_str = g_strdup_printf (_("%0.4lf seconds"), duration / (gdouble)NSEC_PER_SEC);
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

#undef SET_FRAME_COUNT
    }
}

void
sysprof_details_view_add_item (SysprofDetailsView *self,
                               GtkWidget          *left,
                               GtkWidget          *center)
{
  g_return_if_fail (SYSPROF_IS_DETAILS_VIEW (self));
  g_return_if_fail (GTK_IS_WIDGET (left));
  g_return_if_fail (GTK_IS_WIDGET (center));

  gtk_container_add_with_properties (GTK_CONTAINER (self->left_box), left,
                                     "pack-type", GTK_PACK_START,
                                     "expand", TRUE,
                                     "fill", TRUE,
                                     NULL);
  gtk_container_add (GTK_CONTAINER (self->center_box), center);
}
