/* sysprof-symbolizer-private.h
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

#include "sysprof-address-layout-private.h"
#include "sysprof-document.h"
#include "sysprof-document-loader.h"
#include "sysprof-mount-namespace-private.h"
#include "sysprof-process-info-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbol.h"
#include "sysprof-symbolizer.h"

G_BEGIN_DECLS

#define SYSPROF_SYMBOLIZER_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, SYSPROF_TYPE_SYMBOLIZER, SysprofSymbolizerClass)

struct _SysprofSymbolizer
{
  GObject parent;
};

struct _SysprofSymbolizerClass
{
  GObjectClass parent_class;

  void           (*setup)          (SysprofSymbolizer         *self,
                                    SysprofDocumentLoader     *loader);
  void           (*prepare_async)  (SysprofSymbolizer         *self,
                                    SysprofDocument           *document,
                                    GCancellable              *cancellable,
                                    GAsyncReadyCallback        callback,
                                    gpointer                   user_data);
  gboolean       (*prepare_finish) (SysprofSymbolizer         *self,
                                    GAsyncResult              *result,
                                    GError                   **error);
  SysprofSymbol *(*symbolize)      (SysprofSymbolizer         *self,
                                    SysprofStrings            *strings,
                                    const SysprofProcessInfo  *process_info,
                                    SysprofAddressContext      context,
                                    SysprofAddress             address);
};

void           _sysprof_symbolizer_setup          (SysprofSymbolizer         *self,
                                                   SysprofDocumentLoader     *loader);
void           _sysprof_symbolizer_prepare_async  (SysprofSymbolizer         *self,
                                                   SysprofDocument           *document,
                                                   GCancellable              *cancellable,
                                                   GAsyncReadyCallback        callback,
                                                   gpointer                   user_data);
gboolean       _sysprof_symbolizer_prepare_finish (SysprofSymbolizer         *self,
                                                   GAsyncResult              *result,
                                                   GError                   **error);
SysprofSymbol *_sysprof_symbolizer_symbolize      (SysprofSymbolizer         *self,
                                                   SysprofStrings            *strings,
                                                   const SysprofProcessInfo  *process_info,
                                                   SysprofAddressContext      context,
                                                   SysprofAddress             address);

G_END_DECLS
