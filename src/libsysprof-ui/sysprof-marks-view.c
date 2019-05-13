/* sysprof-marks-view.c
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

#define G_LOG_DOMAIN "sysprof-marks-view"

#include "config.h"

#include "sysprof-marks-view.h"

typedef struct
{
  GtkTreeView *tree_view;
} SysprofMarksViewPrivate;

G_DEFINE_TYPE (SysprofMarksView, sysprof_marks_view, GTK_TYPE_BIN)

static void
sysprof_marks_view_class_init (SysprofMarksViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-marks-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, tree_view);
}

static void
sysprof_marks_view_init (SysprofMarksView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_marks_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_MARKS_VIEW, NULL);
}

void
sysprof_marks_view_set_reader (SysprofMarksView     *self,
                               SysprofCaptureReader *reader)
{
  g_return_if_fail (SYSPROF_IS_MARKS_VIEW (self));
}
