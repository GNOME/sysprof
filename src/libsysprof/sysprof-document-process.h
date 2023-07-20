/* sysprof-document-process.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <gio/gio.h>

#include "sysprof-document-frame.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_PROCESS         (sysprof_document_process_get_type())
#define SYSPROF_IS_DOCUMENT_PROCESS(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_PROCESS)
#define SYSPROF_DOCUMENT_PROCESS(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_PROCESS, SysprofDocumentProcess)
#define SYSPROF_DOCUMENT_PROCESS_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_PROCESS, SysprofDocumentProcessClass)

typedef struct _SysprofDocumentProcess      SysprofDocumentProcess;
typedef struct _SysprofDocumentProcessClass SysprofDocumentProcessClass;

SYSPROF_AVAILABLE_IN_ALL
GType       sysprof_document_process_get_type         (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_process_get_command_line (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
gint64      sysprof_document_process_get_exit_time    (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
gint64      sysprof_document_process_get_duration     (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
char       *sysprof_document_process_dup_title        (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel *sysprof_document_process_list_memory_maps (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel *sysprof_document_process_list_mounts      (SysprofDocumentProcess *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel *sysprof_document_process_list_threads     (SysprofDocumentProcess *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentProcess, g_object_unref)

G_END_DECLS

