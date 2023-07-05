/* sysprof-callgraph.h
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

#include "sysprof-callgraph-frame.h"
#include "sysprof-callgraph-symbol.h"
#include "sysprof-document-frame.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_CALLGRAPH (sysprof_callgraph_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofCallgraph, sysprof_callgraph, SYSPROF, CALLGRAPH, GObject)

typedef struct _SysprofCallgraphNode SysprofCallgraphNode;

/**
 * SysprofAugmentationFunc:
 * @callgraph: the callgraph being augmented
 * @node: the node within the callgraph
 * @frame: the frame used to generate this node
 * @summarize: if summaries should be generated
 * @user_data: closure data for augmentation func
 *
 * This function is called for the bottom most node in a trace as it is added
 * to a callgraph.
 *
 * The augmentation func should augment the node in whatever way it sees fit
 * and generally will want to walk up the node tree to the root to augment the
 * parents as it goes. Your augmentation function is not called for each node,
 * only the deepest node.
 *
 * If @summarize is %TRUE, then you should also generate summary augmentation
 * using sysprof_callgraph_get_summary_augment() or similar.
 */
typedef void (*SysprofAugmentationFunc) (SysprofCallgraph     *callgraph,
                                         SysprofCallgraphNode *node,
                                         SysprofDocumentFrame *frame,
                                         gboolean              summarize,
                                         gpointer              user_data);

typedef enum _SysprofCallgraphFlags
{
  SYSPROF_CALLGRAPH_FLAGS_NONE = 0,
  SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS = 1 << 1,
} SysprofCallgraphFlags;

SYSPROF_AVAILABLE_IN_ALL
GType                 sysprof_callgraph_flags_get_type             (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
GListModel           *sysprof_callgraph_list_symbols               (SysprofCallgraph       *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel           *sysprof_callgraph_list_callers               (SysprofCallgraph       *self,
                                                                    SysprofSymbol          *symbol);
SYSPROF_AVAILABLE_IN_ALL
GListModel           *sysprof_callgraph_list_traceables_for_symbol (SysprofCallgraph       *self,
                                                                    SysprofSymbol          *symbol);
SYSPROF_AVAILABLE_IN_ALL
GListModel           *sysprof_callgraph_list_traceables_for_symbols_matching (SysprofCallgraph *self,
                                                                              const char       *pattern);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_callgraph_descendants_async          (SysprofCallgraph       *self,
                                                                    SysprofSymbol          *symbol,
                                                                    GCancellable           *cancellable,
                                                                    GAsyncReadyCallback     callback,
                                                                    gpointer                user_data);
SYSPROF_AVAILABLE_IN_ALL
GListModel           *sysprof_callgraph_descendants_finish         (SysprofCallgraph        *self,
                                                                    GAsyncResult            *result,
                                                                    GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
gpointer              sysprof_callgraph_get_augment                (SysprofCallgraph       *self,
                                                                    SysprofCallgraphNode   *node);
SYSPROF_AVAILABLE_IN_ALL
gpointer              sysprof_callgraph_get_summary_augment        (SysprofCallgraph       *self,
                                                                    SysprofCallgraphNode   *node);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraphNode *sysprof_callgraph_node_parent                (SysprofCallgraphNode   *node);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraph     *sysprof_callgraph_frame_get_callgraph        (SysprofCallgraphFrame  *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraph     *sysprof_callgraph_symbol_get_callgraph       (SysprofCallgraphSymbol *self);

G_END_DECLS
