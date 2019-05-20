/* sysprof-ui-private.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-callgraph-view.h"
#include "sysprof-marks-view.h"
#include "sysprof-visualizer-view.h"

G_BEGIN_DECLS

typedef struct
{
  gchar   *name;
  guint64  count;
  gint64   max;
  gint64   min;
  gint64   avg;
  guint64  avg_count;
} SysprofMarkStat;

SysprofMarkStat *_sysprof_mark_stat_new                   (const gchar           *name);
void             _sysprof_mark_stat_free                  (SysprofMarkStat       *self);
void             _sysprof_marks_view_set_hadjustment      (SysprofMarksView      *self,
                                                           GtkAdjustment         *hadjustment);
void             _sysprof_visualizer_view_set_hadjustment (SysprofVisualizerView *self,
                                                           GtkAdjustment         *hadjustment);
void             _sysprof_rounded_rectangle               (cairo_t               *cr,
                                                           const GdkRectangle    *rect,
                                                           gint                   x_radius,
                                                           gint                   y_radius);
gchar           *_sysprof_format_duration                 (gint64                 duration);
void             _sysprof_callgraph_view_set_failed       (SysprofCallgraphView  *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofMarkStat, _sysprof_mark_stat_free)

#if !GLIB_CHECK_VERSION(2, 56, 0)
# define g_clear_weak_pointer(ptr) \
   (*(ptr) ? (g_object_remove_weak_pointer((GObject*)*(ptr), (gpointer*)ptr),*(ptr)=NULL,1) : 0)
# define g_set_weak_pointer(ptr,obj) \
   ((obj!=*(ptr))?(g_clear_weak_pointer(ptr),*(ptr)=obj,((obj)?g_object_add_weak_pointer((GObject*)obj,(gpointer*)ptr),NULL:NULL),1):0)
#endif

G_END_DECLS
