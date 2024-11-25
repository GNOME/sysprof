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

#include <eggbitset.h>

#include <libdex.h>

#include "sysprof-document.h"
#include "sysprof-symbolizer.h"
#include "sysprof-symbol.h"

G_BEGIN_DECLS

typedef struct _SysprofDocumentFramePointer
{
  guint64 offset : 48;
  guint64 length : 16;
} SysprofDocumentFramePointer;

typedef struct _SysprofDocumentTimedValue
{
  gint64 time;
  union {
    gint64 v_int64;
    double v_double;
    guint8 v_raw[8];
  };
} SysprofDocumentTimedValue;

typedef void (*ProgressFunc) (double      fraction,
                              const char *message,
                              gpointer    user_data);

void             _sysprof_document_new_async        (GMappedFile          *mapped_file,
                                                     ProgressFunc          progress,
                                                     gpointer              progress_data,
                                                     GDestroyNotify        progress_data_destroy,
                                                     GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
SysprofDocument *_sysprof_document_new_finish       (GAsyncResult         *result,
                                                     GError              **error);
void             _sysprof_document_set_title        (SysprofDocument      *self,
                                                     const char           *title);
void             _sysprof_document_symbolize_async  (SysprofDocument      *self,
                                                     SysprofSymbolizer    *symbolizer,
                                                     ProgressFunc          progress_func,
                                                     gpointer              progress_data,
                                                     GDestroyNotify        progress_data_destroy,
                                                     GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean         _sysprof_document_symbolize_finish (SysprofDocument      *self,
                                                     GAsyncResult         *result,
                                                     GError              **error);
gboolean         _sysprof_document_is_native        (SysprofDocument      *self);
GRefString      *_sysprof_document_ref_string       (SysprofDocument      *self,
                                                     const char           *name);
EggBitset       *_sysprof_document_traceables       (SysprofDocument      *self);
SysprofSymbol   *_sysprof_document_process_symbol   (SysprofDocument      *self,
                                                     int                   pid,
                                                     gboolean              want_shared);
SysprofSymbol   *_sysprof_document_thread_symbol    (SysprofDocument      *self,
                                                     int                   pid,
                                                     int                   tid);
SysprofSymbol   *_sysprof_document_kernel_symbol    (SysprofDocument      *self);
GArray          *_sysprof_document_get_frames       (SysprofDocument      *self);
EggBitset       *_sysprof_document_get_allocations  (SysprofDocument      *self);
DexFuture       *_sysprof_document_serialize_symbols(SysprofDocument      *self);

G_END_DECLS
