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

typedef enum _SysprofCallgraphCategory
{
  SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED = 1,
  SYSPROF_CALLGRAPH_CATEGORY_PRESENTATION,
  SYSPROF_CALLGRAPH_CATEGORY_A11Y,
  SYSPROF_CALLGRAPH_CATEGORY_ACTIONS,
  SYSPROF_CALLGRAPH_CATEGORY_CONTEXT_SWITCH,
  SYSPROF_CALLGRAPH_CATEGORY_COREDUMP,
  SYSPROF_CALLGRAPH_CATEGORY_CSS,
  SYSPROF_CALLGRAPH_CATEGORY_GRAPHICS,
  SYSPROF_CALLGRAPH_CATEGORY_ICONS,
  SYSPROF_CALLGRAPH_CATEGORY_INPUT,
  SYSPROF_CALLGRAPH_CATEGORY_IO,
  SYSPROF_CALLGRAPH_CATEGORY_IPC,
  SYSPROF_CALLGRAPH_CATEGORY_JAVASCRIPT,
  SYSPROF_CALLGRAPH_CATEGORY_KERNEL,
  SYSPROF_CALLGRAPH_CATEGORY_LAYOUT,
  SYSPROF_CALLGRAPH_CATEGORY_LOCKING,
  SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP,
  SYSPROF_CALLGRAPH_CATEGORY_MEMORY,
  SYSPROF_CALLGRAPH_CATEGORY_PAINT,
  SYSPROF_CALLGRAPH_CATEGORY_TYPE_SYSTEM,
  SYSPROF_CALLGRAPH_CATEGORY_UNWINDABLE,
  SYSPROF_CALLGRAPH_CATEGORY_WINDOWING,

  /* Not part of ABI */
  SYSPROF_CALLGRAPH_CATEGORY_LAST,
} SysprofCallgraphCategory;

typedef enum _SysprofCallgraphFlags
{
  SYSPROF_CALLGRAPH_FLAGS_NONE                    = 0,
  SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS         = 1 << 1,
  SYSPROF_CALLGRAPH_FLAGS_HIDE_SYSTEM_LIBRARIES   = 1 << 2,
  SYSPROF_CALLGRAPH_FLAGS_BOTTOM_UP               = 1 << 3,
  SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES       = 1 << 4,
  SYSPROF_CALLGRAPH_FLAGS_IGNORE_PROCESS_0        = 1 << 5,
  SYSPROF_CALLGRAPH_FLAGS_LEFT_HEAVY              = 1 << 6,
  SYSPROF_CALLGRAPH_FLAGS_MERGE_SIMILAR_PROCESSES = 1 << 7,
  SYSPROF_CALLGRAPH_FLAGS_IGNORE_KERNEL_PROCESSES = 1 << 8,
} SysprofCallgraphFlags;

SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_list_symbols                         (SysprofCallgraph        *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_list_callers                         (SysprofCallgraph        *self,
                                                                                  SysprofSymbol           *symbol);
SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_list_traceables_for_symbol           (SysprofCallgraph        *self,
                                                                                  SysprofSymbol           *symbol);
SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_list_traceables_for_symbols_matching (SysprofCallgraph        *self,
                                                                                  const char              *pattern);
SYSPROF_AVAILABLE_IN_ALL
void                      sysprof_callgraph_descendants_async                    (SysprofCallgraph        *self,
                                                                                  SysprofSymbol           *symbol,
                                                                                  GCancellable            *cancellable,
                                                                                  GAsyncReadyCallback      callback,
                                                                                  gpointer                 user_data);
SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_descendants_finish                   (SysprofCallgraph        *self,
                                                                                  GAsyncResult            *result,
                                                                                  GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
gpointer                  sysprof_callgraph_get_augment                          (SysprofCallgraph        *self,
                                                                                  SysprofCallgraphNode    *node);
SYSPROF_AVAILABLE_IN_ALL
gpointer                  sysprof_callgraph_get_summary_augment                  (SysprofCallgraph        *self,
                                                                                  SysprofCallgraphNode    *node);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraphNode     *sysprof_callgraph_node_parent                          (SysprofCallgraphNode    *node);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraph         *sysprof_callgraph_frame_get_callgraph                  (SysprofCallgraphFrame   *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraphCategory  sysprof_callgraph_frame_get_category                   (SysprofCallgraphFrame   *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCallgraph         *sysprof_callgraph_symbol_get_callgraph                 (SysprofCallgraphSymbol  *self);
SYSPROF_AVAILABLE_IN_ALL
void                      sysprof_callgraph_list_traceables_for_node_async       (SysprofCallgraph        *self,
                                                                                  SysprofCallgraphNode    *node,
                                                                                  GCancellable            *cancellable,
                                                                                  GAsyncReadyCallback      callback,
                                                                                  gpointer                 user_data);
SYSPROF_AVAILABLE_IN_ALL
GListModel               *sysprof_callgraph_list_traceables_for_node_finish      (SysprofCallgraph        *self,
                                                                                  GAsyncResult            *result,
                                                                                  GError                 **error);

G_END_DECLS
