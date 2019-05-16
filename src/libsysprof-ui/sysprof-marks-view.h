/* sysprof-marks-view.h
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

#pragma once

#include <sysprof.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARKS_VIEW (sysprof_marks_view_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofMarksView, sysprof_marks_view, SYSPROF, MARKS_VIEW, GtkBin)

struct _SysprofMarksViewClass
{
  GtkBinClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget *sysprof_marks_view_new         (void);
SYSPROF_AVAILABLE_IN_ALL
void       sysprof_marks_view_load_async  (SysprofMarksView      *self,
                                           SysprofCaptureReader  *reader,
                                           SysprofSelection      *selection,
                                           GCancellable          *cancellable,
                                           GAsyncReadyCallback    callback,
                                           gpointer               user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean   sysprof_marks_view_load_finish (SysprofMarksView      *self,
                                           GAsyncResult          *result,
                                           GError               **error);

G_END_DECLS
