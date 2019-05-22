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
#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARKS_MODEL      (sysprof_marks_model_get_type())
#define SYSPROF_TYPE_MARKS_MODEL_KIND (sysprof_marks_model_kind_get_type())

typedef enum
{
  SYSPROF_MARKS_MODEL_COLUMN_GROUP,
  SYSPROF_MARKS_MODEL_COLUMN_NAME,
  SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME,
  SYSPROF_MARKS_MODEL_COLUMN_END_TIME,
  SYSPROF_MARKS_MODEL_COLUMN_DURATION,
  SYSPROF_MARKS_MODEL_COLUMN_TEXT,
} SysprofMarksModelColumn;

typedef enum
{
  SYSPROF_MARKS_MODEL_MARKS = 1,
  SYSPROF_MARKS_MODEL_COUNTERS,
  SYSPROF_MARKS_MODEL_BOTH = SYSPROF_MARKS_MODEL_MARKS | SYSPROF_MARKS_MODEL_COUNTERS,
} SysprofMarksModelKind;

#define SYSPROF_MARKS_MODEL_COLUMN_LAST (SYSPROF_MARKS_MODEL_COLUMN_TEXT+1)

G_DECLARE_FINAL_TYPE (SysprofMarksModel, sysprof_marks_model, SYSPROF, MARKS_MODEL, GObject)

GType              sysprof_marks_model_kind_get_type (void) G_GNUC_CONST;
void               sysprof_marks_model_new_async     (SysprofCaptureReader   *reader,
                                                      SysprofMarksModelKind   kind,
                                                      SysprofSelection       *selection,
                                                      GCancellable           *cancellable,
                                                      GAsyncReadyCallback     callback,
                                                      gpointer                user_data);
SysprofMarksModel *sysprof_marks_model_new_finish    (GAsyncResult           *result,
                                                      GError                **error);
void               sysprof_marks_model_get_range     (SysprofMarksModel      *self,
                                                      gint64                 *begin_time,
                                                      gint64                 *end_time);

G_END_DECLS
