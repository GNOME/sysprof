/* sysprof-document-storage-private.h
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-document-private.h"
#include "sysprof-frame-cycle.h"

G_BEGIN_DECLS

typedef struct _SysprofDocumentStorage SysprofDocumentStorage;

typedef struct
{
  char   *category;
  char   *name;
  char   *description;
  guint   n_values;
  double  minimum;
  double  maximum;
} SysprofCounterMetadata;

typedef struct
{
  char   *title;
  char   *command;
  int     pid;
  gint64  begin;
  gint64  end;
} SysprofProcessMetadata;

typedef enum
{
  SYSPROF_DOCUMENT_COUNT_MARKS,
  SYSPROF_DOCUMENT_COUNT_LOGS,
  SYSPROF_DOCUMENT_COUNT_PROCESSES,
  SYSPROF_DOCUMENT_COUNT_SAMPLES,
  SYSPROF_DOCUMENT_COUNT_DBUS,
  SYSPROF_DOCUMENT_COUNT_FILES,
  SYSPROF_DOCUMENT_COUNT_DIAGNOSTICS,
  SYSPROF_DOCUMENT_N_COUNTS
} SysprofDocumentCount;

#define SYSPROF_TYPE_DOCUMENT_READER (_sysprof_document_reader_get_type())
G_DECLARE_FINAL_TYPE (SysprofDocumentReader, _sysprof_document_reader, SYSPROF, DOCUMENT_READER, GObject)

SysprofDocumentStorage       *_sysprof_document_dup_storage              (SysprofDocument        *document);
SysprofFrameCycle            *_sysprof_document_reader_create_frames     (SysprofDocumentReader  *self);
GCancellable                 *_sysprof_document_reader_get_cancellable   (SysprofDocumentReader  *self);
guint                         _sysprof_document_reader_get_count         (SysprofDocumentReader  *self,
                                                                          SysprofDocumentCount    count);
const SysprofCounterMetadata *_sysprof_document_reader_get_counters      (SysprofDocumentReader  *self,
                                                                          guint                  *n_counters);
const char                   *_sysprof_document_reader_get_metadata      (SysprofDocumentReader  *self,
                                                                          const char             *key);
const SysprofProcessMetadata *_sysprof_document_reader_get_processes     (SysprofDocumentReader  *self,
                                                                          guint                  *n_processes);
gboolean                      _sysprof_document_reader_is_cancelled      (SysprofDocumentReader  *self);
GListModel                   *_sysprof_document_reader_list_files        (SysprofDocumentReader  *self);
GListModel                   *_sysprof_document_reader_list_logs         (SysprofDocumentReader  *self);
GListModel                   *_sysprof_document_reader_list_marks        (SysprofDocumentReader  *self,
                                                                          const char             *group);
GListModel                   *_sysprof_document_reader_list_named_marks  (SysprofDocumentReader  *self,
                                                                          const char * const     *names);
SysprofDocumentReader        *_sysprof_document_reader_new               (SysprofDocumentStorage *storage,
                                                                          GCancellable           *cancellable);
EggBitset                    *_sysprof_document_storage_copy_logs        (SysprofDocumentStorage *self);
EggBitset                    *_sysprof_document_storage_copy_marks       (SysprofDocumentStorage *self,
                                                                          const char             *group);
EggBitset                    *_sysprof_document_storage_copy_named_marks (SysprofDocumentStorage *self,
                                                                          const char * const     *names);
SysprofFrameCycle            *_sysprof_document_storage_dup_frames       (SysprofDocumentStorage *self);
guint                         _sysprof_document_storage_get_count        (SysprofDocumentStorage *self,
                                                                          SysprofDocumentCount    count);
const SysprofCounterMetadata *_sysprof_document_storage_get_counters     (SysprofDocumentStorage *self,
                                                                          guint                  *n_counters);
SysprofDocumentFrame         *_sysprof_document_storage_get_frame        (SysprofDocumentStorage *self,
                                                                          guint                   position);
const char                   *_sysprof_document_storage_get_metadata     (SysprofDocumentStorage *self,
                                                                          const char             *key);
guint                         _sysprof_document_storage_get_n_frames     (SysprofDocumentStorage *self);
const SysprofProcessMetadata *_sysprof_document_storage_get_processes    (SysprofDocumentStorage *self,
                                                                          guint                  *n_processes);
GListModel                   *_sysprof_document_storage_list_files       (SysprofDocumentStorage *self,
                                                                          GCancellable           *cancellable);
SysprofDocumentStorage       *_sysprof_document_storage_ref              (SysprofDocumentStorage *self);
void                          _sysprof_document_storage_unref            (SysprofDocumentStorage *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentStorage, _sysprof_document_storage_unref)

G_END_DECLS
