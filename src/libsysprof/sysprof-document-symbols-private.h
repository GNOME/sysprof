/* sysprof-document-symbols-private.h
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

#include "sysprof-document.h"
#include "sysprof-process-info-private.h"
#include "sysprof-symbol-cache-private.h"
#include "sysprof-symbol.h"
#include "sysprof-symbolizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_SYMBOLS (sysprof_document_symbols_get_type())

G_DECLARE_FINAL_TYPE (SysprofDocumentSymbols, sysprof_document_symbols, SYSPROF, DOCUMENT_SYMBOLS, GObject)

struct _SysprofDocumentSymbols
{
  GObject             parent_instance;
  SysprofSymbol      *context_switches[SYSPROF_ADDRESS_CONTEXT_GUEST_USER+1];
  SysprofSymbolCache *kernel_symbols;
};

void                    _sysprof_document_symbols_new        (SysprofDocument           *document,
                                                              SysprofStrings            *strings,
                                                              SysprofSymbolizer         *symbolizer,
                                                              GHashTable                *pid_to_process_info,
                                                              ProgressFunc               progress_func,
                                                              gpointer                   progress_data,
                                                              GDestroyNotify             progress_data_destroy,
                                                              GCancellable              *cancellable,
                                                              GAsyncReadyCallback        callback,
                                                              gpointer                   user_data);
SysprofDocumentSymbols *_sysprof_document_symbols_new_finish (GAsyncResult              *result,
                                                              GError                   **error);
SysprofSymbol          *_sysprof_document_symbols_lookup     (SysprofDocumentSymbols    *symbols,
                                                              const SysprofProcessInfo  *process_info,
                                                              SysprofAddressContext      context,
                                                              SysprofAddress             address);

G_END_DECLS
