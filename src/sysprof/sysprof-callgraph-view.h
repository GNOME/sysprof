/* sysprof-callgraph-view.h
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

#include <sysprof-capture.h>
#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_CALLGRAPH_VIEW         (sysprof_callgraph_view_get_type())
#define SYSPROF_IS_CALLGRAPH_VIEW(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_CALLGRAPH_VIEW)
#define SYSPROF_CALLGRAPH_VIEW(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_CALLGRAPH_VIEW, SysprofCallgraphView)
#define SYSPROF_CALLGRAPH_VIEW_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_CALLGRAPH_VIEW, SysprofCallgraphViewClass)

typedef struct _SysprofCallgraphView      SysprofCallgraphView;
typedef struct _SysprofCallgraphViewClass SysprofCallgraphViewClass;

GType             sysprof_callgraph_view_get_type                    (void) G_GNUC_CONST;
SysprofCallgraph *sysprof_callgraph_view_get_callgraph               (SysprofCallgraphView *self);
SysprofDocument  *sysprof_callgraph_view_get_document                (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_document                (SysprofCallgraphView *self,
                                                                      SysprofDocument      *document);
GListModel       *sysprof_callgraph_view_get_traceables              (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_traceables              (SysprofCallgraphView *self,
                                                                      GListModel           *model);
gboolean          sysprof_callgraph_view_get_bottom_up               (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_bottom_up               (SysprofCallgraphView *self,
                                                                      gboolean              bottom_up);
gboolean          sysprof_callgraph_view_get_categorize_frames       (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_categorize_frames       (SysprofCallgraphView *self,
                                                                      gboolean              categorize_frames);
gboolean          sysprof_callgraph_view_get_ignore_process_0        (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_ignore_process_0        (SysprofCallgraphView *self,
                                                                      gboolean              ignore_process_0);
gboolean          sysprof_callgraph_view_get_ignore_kernel_processes (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_ignore_kernel_processes (SysprofCallgraphView *self,
                                                                      gboolean              ignore_kernel_processes);
gboolean          sysprof_callgraph_view_get_include_threads         (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_include_threads         (SysprofCallgraphView *self,
                                                                      gboolean              include_threads);
gboolean          sysprof_callgraph_view_get_hide_system_libraries   (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_hide_system_libraries   (SysprofCallgraphView *self,
                                                                      gboolean              hide_system_libraries);
gboolean          sysprof_callgraph_view_get_left_heavy              (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_left_heavy              (SysprofCallgraphView *self,
                                                                      gboolean              left_heavy);
gboolean          sysprof_callgraph_view_get_merge_similar_processes (SysprofCallgraphView *self);
void              sysprof_callgraph_view_set_merge_similar_processes (SysprofCallgraphView *self,
                                                                      gboolean              merge_similar_processes);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCallgraphView, g_object_unref)

G_END_DECLS
