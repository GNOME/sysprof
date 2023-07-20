/* sysprof-document-exit.h
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

#define SYSPROF_TYPE_DOCUMENT_EXIT         (sysprof_document_exit_get_type())
#define SYSPROF_IS_DOCUMENT_EXIT(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_EXIT)
#define SYSPROF_DOCUMENT_EXIT(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_EXIT, SysprofDocumentExit)
#define SYSPROF_DOCUMENT_EXIT_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_EXIT, SysprofDocumentExitClass)

typedef struct _SysprofDocumentExit      SysprofDocumentExit;
typedef struct _SysprofDocumentExitClass SysprofDocumentExitClass;

SYSPROF_AVAILABLE_IN_ALL
GType sysprof_document_exit_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentExit, g_object_unref)

G_END_DECLS

