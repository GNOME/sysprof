/* sysprof-log-model.h
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

G_BEGIN_DECLS

typedef enum
{
  SYSPROF_LOG_MODEL_COLUMN_TIME,
  SYSPROF_LOG_MODEL_COLUMN_SEVERITY,
  SYSPROF_LOG_MODEL_COLUMN_DOMAIN,
  SYSPROF_LOG_MODEL_COLUMN_MESSAGE,
  SYSPROF_LOG_MODEL_COLUMN_TIME_STRING,
  SYSPROF_LOG_MODEL_COLUMN_LAST
} SysprofLogModelColumn;

#define SYSPROF_TYPE_LOG_MODEL (sysprof_log_model_get_type())

G_DECLARE_FINAL_TYPE (SysprofLogModel, sysprof_log_model, SYSPROF, LOG_MODEL, GObject)

void             sysprof_log_model_new_async  (SysprofCaptureReader  *reader,
                                               SysprofSelection      *selection,
                                               GCancellable          *cancellable,
                                               GAsyncReadyCallback    callback,
                                               gpointer               user_data);
SysprofLogModel *sysprof_log_model_new_finish (GAsyncResult          *result,
                                               GError               **error);

G_END_DECLS
