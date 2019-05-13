/* sysprof-marks-model.h
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

#include <gtk/gtk.h>
#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARKS_MODEL (sysprof_marks_model_get_type())

typedef enum
{
  SYSPROF_MARKS_MODEL_COLUMN_TEXT,
  SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME,
  SYSPROF_MARKS_MODEL_COLUMN_END_TIME,
} SysprofMarksModelColumn;

G_DECLARE_FINAL_TYPE (SysprofMarksModel, sysprof_marks_model, SYSPROF, MARKS_MODEL, GObject)

void               sysprof_marks_model_new_async  (SysprofCaptureReader  *reader,
                                                   GCancellable          *cancellable,
                                                   GAsyncReadyCallback    callback,
                                                   gpointer               user_data);
SysprofMarksModel *sysprof_marks_model_new_finish (GAsyncResult          *result,
                                                   GError               **error);

G_END_DECLS
