/* sysprof-callgraph-frame.h
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

#include "sysprof-symbol.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_CALLGRAPH_FRAME (sysprof_callgraph_frame_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofCallgraphFrame, sysprof_callgraph_frame, SYSPROF, CALLGRAPH_FRAME, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSymbol *sysprof_callgraph_frame_get_symbol             (SysprofCallgraphFrame  *self);
SYSPROF_AVAILABLE_IN_ALL
gpointer       sysprof_callgraph_frame_get_augment            (SysprofCallgraphFrame  *self);
SYSPROF_AVAILABLE_IN_ALL
gpointer       sysprof_callgraph_frame_get_summary_augment    (SysprofCallgraphFrame  *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_callgraph_frame_list_traceables_async  (SysprofCallgraphFrame  *self,
                                                               GCancellable           *cancellable,
                                                               GAsyncReadyCallback     callback,
                                                               gpointer                user_data);
SYSPROF_AVAILABLE_IN_ALL
GListModel    *sysprof_callgraph_frame_list_traceables_finish (SysprofCallgraphFrame  *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sysprof_callgraph_frame_is_leaf                (SysprofCallgraphFrame  *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_callgraph_frame_summarize_async        (SysprofCallgraphFrame  *self,
                                                               GCancellable           *cancellable,
                                                               GAsyncReadyCallback     callback,
                                                               gpointer                user_data);
SYSPROF_AVAILABLE_IN_ALL
GListModel    *sysprof_callgraph_frame_summarize_finish       (SysprofCallgraphFrame  *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);

G_END_DECLS
