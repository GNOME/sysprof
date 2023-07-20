/* sysprof-document-file-chunk.h
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

#define SYSPROF_TYPE_DOCUMENT_FILE_CHUNK         (sysprof_document_file_chunk_get_type())
#define SYSPROF_IS_DOCUMENT_FILE_CHUNK(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_FILE_CHUNK)
#define SYSPROF_DOCUMENT_FILE_CHUNK(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_FILE_CHUNK, SysprofDocumentFileChunk)
#define SYSPROF_DOCUMENT_FILE_CHUNK_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_FILE_CHUNK, SysprofDocumentFileChunkClass)

typedef struct _SysprofDocumentFileChunk      SysprofDocumentFileChunk;
typedef struct _SysprofDocumentFileChunkClass SysprofDocumentFileChunkClass;

SYSPROF_AVAILABLE_IN_ALL
GType         sysprof_document_file_chunk_get_type     (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
gboolean      sysprof_document_file_chunk_get_is_last  (SysprofDocumentFileChunk *self);
SYSPROF_AVAILABLE_IN_ALL
guint         sysprof_document_file_chunk_get_size     (SysprofDocumentFileChunk *self);
SYSPROF_AVAILABLE_IN_ALL
const char   *sysprof_document_file_chunk_get_path     (SysprofDocumentFileChunk *self);
SYSPROF_AVAILABLE_IN_ALL
const guint8 *sysprof_document_file_chunk_get_data     (SysprofDocumentFileChunk *self,
                                                        guint                    *size);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentFileChunk, g_object_unref)

G_END_DECLS
