/* sysprof-document-frame.h
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

#include <glib-object.h>

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_FRAME         (sysprof_document_frame_get_type())
#define SYSPROF_IS_DOCUMENT_FRAME(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_FRAME)
#define SYSPROF_DOCUMENT_FRAME(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_FRAME, SysprofDocumentFrame)
#define SYSPROF_DOCUMENT_FRAME_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_FRAME, SysprofDocumentFrameClass)

typedef struct _SysprofDocumentFrame      SysprofDocumentFrame;
typedef struct _SysprofDocumentFrameClass SysprofDocumentFrameClass;

SYSPROF_AVAILABLE_IN_ALL
GType     sysprof_document_frame_get_type        (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
int       sysprof_document_frame_get_cpu         (SysprofDocumentFrame       *self);
SYSPROF_AVAILABLE_IN_ALL
int       sysprof_document_frame_get_pid         (SysprofDocumentFrame       *self);
SYSPROF_AVAILABLE_IN_ALL
gint64    sysprof_document_frame_get_time        (SysprofDocumentFrame       *self);
SYSPROF_AVAILABLE_IN_ALL
gint64    sysprof_document_frame_get_time_offset (SysprofDocumentFrame       *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean  sysprof_document_frame_equal           (const SysprofDocumentFrame *a,
                                                  const SysprofDocumentFrame *b);
SYSPROF_AVAILABLE_IN_ALL
char     *sysprof_document_frame_dup_tooltip     (SysprofDocumentFrame       *self);
SYSPROF_AVAILABLE_IN_ALL
char     *sysprof_document_frame_dup_time_string (SysprofDocumentFrame       *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentFrame, g_object_unref)

G_END_DECLS
