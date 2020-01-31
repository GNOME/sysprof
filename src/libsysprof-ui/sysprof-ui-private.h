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

#include "sysprof-callgraph-page.h"
#include "sysprof-display.h"
#include "sysprof-profiler-assistant.h"

G_BEGIN_DECLS

void   _sysprof_callgraph_page_set_failed       (SysprofCallgraphPage     *self);
void   _sysprof_callgraph_page_set_loading      (SysprofCallgraphPage     *self,
                                                 gboolean                  loading);
void   _sysprof_memory_page_set_failed          (SysprofCallgraphPage     *self);
void   _sysprof_memory_page_set_loading         (SysprofCallgraphPage     *self,
                                                 gboolean                  loading);
void   _sysprof_display_focus_record            (SysprofDisplay           *self);
void   _sysprof_profiler_assistant_focus_record (SysprofProfilerAssistant *self);
gchar *_sysprof_format_duration                 (gint64                    duration);

#if !GLIB_CHECK_VERSION(2, 56, 0)
# define g_clear_weak_pointer(ptr) \
   (*(ptr) ? (g_object_remove_weak_pointer((GObject*)*(ptr), (gpointer*)ptr),*(ptr)=NULL,1) : 0)
# define g_set_weak_pointer(ptr,obj) \
   ((obj!=*(ptr))?(g_clear_weak_pointer(ptr),*(ptr)=obj,((obj)?g_object_add_weak_pointer((GObject*)obj,(gpointer*)ptr),NULL:NULL),1):0)
#endif

G_END_DECLS
