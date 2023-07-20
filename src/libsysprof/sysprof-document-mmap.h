/*
 * sysprof-document-mmap.h
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

#define SYSPROF_TYPE_DOCUMENT_MMAP         (sysprof_document_mmap_get_type())
#define SYSPROF_IS_DOCUMENT_MMAP(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_MMAP)
#define SYSPROF_DOCUMENT_MMAP(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_MMAP, SysprofDocumentMmap)
#define SYSPROF_DOCUMENT_MMAP_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_MMAP, SysprofDocumentMmapClass)

typedef struct _SysprofDocumentMmap      SysprofDocumentMmap;
typedef struct _SysprofDocumentMmapClass SysprofDocumentMmapClass;

SYSPROF_AVAILABLE_IN_ALL
GType       sysprof_document_mmap_get_type          (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
guint64     sysprof_document_mmap_get_start_address (SysprofDocumentMmap *self);
SYSPROF_AVAILABLE_IN_ALL
guint64     sysprof_document_mmap_get_end_address   (SysprofDocumentMmap *self);
SYSPROF_AVAILABLE_IN_ALL
guint64     sysprof_document_mmap_get_file_inode    (SysprofDocumentMmap *self);
SYSPROF_AVAILABLE_IN_ALL
guint64     sysprof_document_mmap_get_file_offset   (SysprofDocumentMmap *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_mmap_get_file          (SysprofDocumentMmap *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_mmap_get_build_id      (SysprofDocumentMmap *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentMmap, g_object_unref)

G_END_DECLS
