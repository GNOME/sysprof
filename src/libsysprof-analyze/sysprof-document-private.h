/* sysprof-document-private.h
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

#include <gtk/gtk.h>

#include "sysprof-document.h"

G_BEGIN_DECLS

SysprofDocument        *_sysprof_document_new                (const char           *filename,
                                                              GError              **error);
SysprofDocument        *_sysprof_document_new_from_fd        (int                   capture_fd,
                                                              GError              **error);
void                    _sysprof_document_symbolize_async    (SysprofDocument      *self,
                                                              SysprofSymbolizer    *symbolizer,
                                                              GCancellable         *cancellable,
                                                              GAsyncReadyCallback   callback,
                                                              gpointer              user_data);
SysprofDocumentSymbols *_sysprof_document_symbolize_finish   (SysprofDocument      *self,
                                                              GAsyncResult         *result,
                                                              GError              **error);
gboolean                _sysprof_document_is_native          (SysprofDocument      *self);
char                   *_sysprof_document_ref_string         (SysprofDocument      *self,
                                                              const char           *name);
GtkBitset              *_sysprof_document_traceables         (SysprofDocument      *self);

G_END_DECLS
