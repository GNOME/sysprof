/* sysprof-document-log.h
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

#include "sysprof-document-frame.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_LOG         (sysprof_document_log_get_type())
#define SYSPROF_IS_DOCUMENT_LOG(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_LOG)
#define SYSPROF_DOCUMENT_LOG(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_LOG, SysprofDocumentLog)
#define SYSPROF_DOCUMENT_LOG_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_LOG, SysprofDocumentLogClass)

typedef struct _SysprofDocumentLog      SysprofDocumentLog;
typedef struct _SysprofDocumentLogClass SysprofDocumentLogClass;

SYSPROF_AVAILABLE_IN_ALL
GType           sysprof_document_log_get_type     (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
const char     *sysprof_document_log_get_message  (SysprofDocumentLog *self);
SYSPROF_AVAILABLE_IN_ALL
GLogLevelFlags  sysprof_document_log_get_severity (SysprofDocumentLog *self);
SYSPROF_AVAILABLE_IN_ALL
const char     *sysprof_document_log_get_domain   (SysprofDocumentLog *self);

G_END_DECLS
