/* sysprof-callgraph-view-private.h
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

#include <libpanel.h>

#include "sysprof-callgraph-view.h"

G_BEGIN_DECLS

#define SYSPROF_CALLGRAPH_VIEW_GET_CLASS(instance) G_TYPE_INSTANCE_GET_CLASS(instance, SYSPROF_TYPE_CALLGRAPH_VIEW, SysprofCallgraphViewClass)

struct _SysprofCallgraphView
{
  GtkWidget parent_instance;

  SysprofDocument *document;
  SysprofCallgraph *callgraph;
  GSignalGroup *traceables_signals;
  GListModel *traceables;
  GListModel *utility_traceables;
  GListModel *utility_summary;

  GtkColumnView *callers_column_view;
  GtkColumnView *descendants_column_view;
  GtkColumnView *functions_column_view;
  GtkCustomSorter *descendants_name_sorter;
  GtkCustomSorter *functions_name_sorter;
  GtkScrolledWindow *scrolled_window;
  GtkWidget *paned;
  GtkStringFilter *function_filter;

  GCancellable *cancellable;

  guint reload_source;

  guint bottom_up : 1;
  guint categorize_frames : 1;
  guint hide_system_libraries : 1;
  guint ignore_kernel_processes : 1;
  guint ignore_process_0 : 1;
  guint include_threads : 1;
  guint left_heavy : 1;
  guint merge_similar_processes : 1;
};

struct _SysprofCallgraphViewClass
{
  GtkWidgetClass parent_class;

  gsize augment_size;
  SysprofAugmentationFunc augment_func;

  void (*load)   (SysprofCallgraphView *self,
                  SysprofCallgraph     *callgraph);
  void (*unload) (SysprofCallgraphView *self);
};

G_END_DECLS
