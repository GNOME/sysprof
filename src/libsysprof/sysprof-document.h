/* sysprof-document.h
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

#include "sysprof-callgraph.h"
#include "sysprof-document-counter.h"
#include "sysprof-document-file.h"
#include "sysprof-document-process.h"
#include "sysprof-document-traceable.h"
#include "sysprof-mark-catalog.h"
#include "sysprof-symbol.h"
#include "sysprof-time-span.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT (sysprof_document_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDocument, sysprof_document, SYSPROF, DOCUMENT, GObject)

SYSPROF_AVAILABLE_IN_ALL
char                   *sysprof_document_dup_title                           (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
char                   *sysprof_document_dup_subtitle                        (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofTimeSpan  *sysprof_document_get_time_span                       (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentFile    *sysprof_document_lookup_file                         (SysprofDocument           *self,
                                                                              const char                *path);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentProcess *sysprof_document_lookup_process                      (SysprofDocument           *self,
                                                                              int                        pid);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_cpu_info                       (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_files                          (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_traceables                     (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_allocations                    (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_dbus_messages                  (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_logs                           (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_metadata                       (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_samples                        (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_samples_with_context_switch    (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_samples_without_context_switch (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_processes                      (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_jitmaps                        (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_counters                       (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_marks                          (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_marks_by_group                 (SysprofDocument           *self,
                                                                              const char                *group);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_catalog_marks                       (SysprofDocument           *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentCounter *sysprof_document_find_counter                        (SysprofDocument           *self,
                                                                              const char                *category,
                                                                              const char                *name);
SYSPROF_AVAILABLE_IN_ALL
GListModel             *sysprof_document_list_symbols_in_traceable           (SysprofDocument           *self,
                                                                              SysprofDocumentTraceable  *traceable);
SYSPROF_AVAILABLE_IN_ALL
guint                   sysprof_document_symbolize_traceable                 (SysprofDocument           *self,
                                                                              SysprofDocumentTraceable  *traceable,
                                                                              SysprofSymbol            **symbols,
                                                                              guint                      n_symbols,
                                                                              SysprofAddressContext     *final_context);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_document_callgraph_async                     (SysprofDocument           *self,
                                                                              SysprofCallgraphFlags      flags,
                                                                              GListModel                *traceables,
                                                                              gsize                      augment_size,
                                                                              SysprofAugmentationFunc    augment_func,
                                                                              gpointer                   augment_func_data,
                                                                              GDestroyNotify             augment_func_data_destroy,
                                                                              GCancellable              *cancellable,
                                                                              GAsyncReadyCallback        callback,
                                                                              gpointer                   user_data);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraph       *sysprof_document_callgraph_finish                    (SysprofDocument           *self,
                                                                              GAsyncResult              *result,
                                                                              GError                   **error);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_document_serialize_symbols_async             (SysprofDocument           *self,
                                                                              GCancellable              *cancellable,
                                                                              GAsyncReadyCallback        callback,
                                                                              gpointer                   user_data);
SYSPROF_AVAILABLE_IN_ALL
GBytes                 *sysprof_document_serialize_symbols_finish            (SysprofDocument           *self,
                                                                              GAsyncResult              *result,
                                                                              GError                   **error);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_document_save_async                          (SysprofDocument           *self,
                                                                              GFile                     *file,
                                                                              GCancellable              *cancellable,
                                                                              GAsyncReadyCallback        callback,
                                                                              gpointer                   user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean                sysprof_document_save_finish                         (SysprofDocument           *self,
                                                                              GAsyncResult              *result,
                                                                              GError                   **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean                sysprof_document_get_busy                            (SysprofDocument           *self);

G_END_DECLS
