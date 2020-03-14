/* sysprof-display.h
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

#include <gtk/gtk.h>
#include <sysprof.h>

#include "sysprof-page.h"
#include "sysprof-visualizer-group.h"
#include "sysprof-zoom-manager.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DISPLAY (sysprof_display_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofDisplay, sysprof_display, SYSPROF, DISPLAY, GtkBin)

struct _SysprofDisplayClass
{
  GtkBinClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget          *sysprof_display_new              (void);
SYSPROF_AVAILABLE_IN_ALL
GtkWidget          *sysprof_display_new_for_profiler (SysprofProfiler         *profiler);
SYSPROF_AVAILABLE_IN_ALL
char               *sysprof_display_dup_title        (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofProfiler    *sysprof_display_get_profiler     (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_add_group        (SysprofDisplay          *self,
                                                      SysprofVisualizerGroup  *group);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_add_page         (SysprofDisplay          *self,
                                                      SysprofPage             *page);
SYSPROF_AVAILABLE_IN_ALL
SysprofPage        *sysprof_display_get_visible_page (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_set_visible_page (SysprofDisplay          *self,
                                                      SysprofPage             *page);
SYSPROF_AVAILABLE_IN_ALL
SysprofZoomManager *sysprof_display_get_zoom_manager (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_load_async       (SysprofDisplay          *self,
                                                      SysprofCaptureReader    *reader,
                                                      GCancellable            *cancellable,
                                                      GAsyncReadyCallback      callback,
                                                      gpointer                 user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_display_load_finish      (SysprofDisplay          *self,
                                                      GAsyncResult            *result,
                                                      GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_display_is_empty         (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_open             (SysprofDisplay          *self,
                                                      GFile                   *file);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_save             (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_display_get_can_save     (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_display_stop_recording   (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_display_get_can_replay   (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofDisplay     *sysprof_display_replay           (SysprofDisplay          *self);
SYSPROF_AVAILABLE_IN_3_38
void                sysprof_display_add_to_selection (SysprofDisplay          *self,
                                                      gint64                   begin_time,
                                                      gint64                   end_time);

G_END_DECLS
