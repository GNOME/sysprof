/* sysprof-document-memory-sample.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#define SYSPROF_TYPE_DOCUMENT_MEMORY_SAMPLE         (sysprof_document_memory_sample_get_type())
#define SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_MEMORY_SAMPLE)
#define SYSPROF_DOCUMENT_MEMORY_SAMPLE(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_MEMORY_SAMPLE, SysprofDocumentMemorySample)
#define SYSPROF_DOCUMENT_MEMORY_SAMPLE_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_MEMORY_SAMPLE, SysprofDocumentMemorySampleClass)

typedef struct _SysprofDocumentMemorySample      SysprofDocumentMemorySample;
typedef struct _SysprofDocumentMemorySampleClass SysprofDocumentMemorySampleClass;

SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_address      (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_data_src     (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_event_id     (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint   sysprof_document_memory_sample_get_flags        (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_ip           (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_period       (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
int     sysprof_document_memory_sample_get_tid          (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_time_enabled (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_time_running (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
GType   sysprof_document_memory_sample_get_type         (void);
SYSPROF_AVAILABLE_IN_52
guint64 sysprof_document_memory_sample_get_weight       (SysprofDocumentMemorySample *self);
SYSPROF_AVAILABLE_IN_52
guint   sysprof_document_memory_sample_get_weight_type  (SysprofDocumentMemorySample *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentMemorySample, g_object_unref)

G_END_DECLS
