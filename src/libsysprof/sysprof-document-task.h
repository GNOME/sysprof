/*
 * sysprof-document-task.h
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <glib-object.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_TASK (sysprof_document_task_get_type())

SYSPROF_AVAILABLE_IN_48
G_DECLARE_DERIVABLE_TYPE (SysprofDocumentTask, sysprof_document_task, SYSPROF, DOCUMENT_TASK, GObject)

SYSPROF_AVAILABLE_IN_48
double   sysprof_document_task_get_progress (SysprofDocumentTask *self);
SYSPROF_AVAILABLE_IN_48
char    *sysprof_document_task_dup_message  (SysprofDocumentTask *self);
SYSPROF_AVAILABLE_IN_48
char    *sysprof_document_task_dup_title    (SysprofDocumentTask *self);
SYSPROF_AVAILABLE_IN_48
gboolean sysprof_document_task_is_cancelled (SysprofDocumentTask *self);
SYSPROF_AVAILABLE_IN_48
void     sysprof_document_task_cancel       (SysprofDocumentTask *self);

G_END_DECLS
