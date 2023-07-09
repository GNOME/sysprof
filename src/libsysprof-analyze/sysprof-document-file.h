/* sysprof-document-file.h
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

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_FILE (sysprof_document_file_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDocumentFile, sysprof_document_file, SYSPROF, DOCUMENT_FILE, GObject)

SYSPROF_AVAILABLE_IN_ALL
const char   *sysprof_document_file_get_path      (SysprofDocumentFile *self);
SYSPROF_AVAILABLE_IN_ALL
GBytes       *sysprof_document_file_dup_bytes     (SysprofDocumentFile *self);
SYSPROF_AVAILABLE_IN_ALL
GInputStream *sysprof_document_file_read          (SysprofDocumentFile *self);
SYSPROF_AVAILABLE_IN_ALL
gsize         sysprof_document_file_get_size      (SysprofDocumentFile *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean      sysprof_document_file_is_compressed (SysprofDocumentFile *self);

G_END_DECLS
