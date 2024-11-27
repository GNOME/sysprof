/* sysprof-document-loader.h
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

#include "sysprof-document.h"
#include "sysprof-symbolizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_LOADER (sysprof_document_loader_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDocumentLoader, sysprof_document_loader, SYSPROF, DOCUMENT_LOADER, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentLoader *sysprof_document_loader_new            (const char             *filename);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentLoader *sysprof_document_loader_new_for_fd     (int                     fd,
                                                               GError                **error);
SYSPROF_AVAILABLE_IN_ALL
SysprofSymbolizer     *sysprof_document_loader_get_symbolizer (SysprofDocumentLoader  *self);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_document_loader_set_symbolizer (SysprofDocumentLoader  *self,
                                                               SysprofSymbolizer      *symbolizer);
SYSPROF_AVAILABLE_IN_ALL
double                 sysprof_document_loader_get_fraction   (SysprofDocumentLoader  *self);
SYSPROF_AVAILABLE_IN_48
char                  *sysprof_document_loader_dup_message    (SysprofDocumentLoader  *self);
SYSPROF_DEPRECATED_IN_48_FOR(sysprof_document_loader_dup_message)
const char            *sysprof_document_loader_get_message    (SysprofDocumentLoader  *self);
SYSPROF_AVAILABLE_IN_48
GListModel            *sysprof_document_loader_list_tasks     (SysprofDocumentLoader  *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocument       *sysprof_document_loader_load           (SysprofDocumentLoader  *self,
                                                               GCancellable           *cancellable,
                                                               GError                **error);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_document_loader_load_async     (SysprofDocumentLoader  *self,
                                                               GCancellable           *cancellable,
                                                               GAsyncReadyCallback     callback,
                                                               gpointer                user_data);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocument       *sysprof_document_loader_load_finish    (SysprofDocumentLoader  *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);

G_END_DECLS
