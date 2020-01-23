/* sysprof-visualizers-frame.h
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

#include "sysprof-visualizer-group.h"
#include "sysprof-zoom-manager.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_VISUALIZERS_FRAME (sysprof_visualizers_frame_get_type())

G_DECLARE_FINAL_TYPE (SysprofVisualizersFrame, sysprof_visualizers_frame, SYSPROF, VISUALIZERS_FRAME, GtkBin)

SysprofSelection       *sysprof_visualizers_frame_get_selection      (SysprofVisualizersFrame  *self);
SysprofVisualizerGroup *sysprof_visualizers_frame_get_selected_group (SysprofVisualizersFrame  *self);
SysprofZoomManager     *sysprof_visualizers_frame_get_zoom_manager   (SysprofVisualizersFrame  *self);
void                    sysprof_visualizers_frame_load_async         (SysprofVisualizersFrame  *self,
                                                                      SysprofCaptureReader     *reader,
                                                                      GCancellable             *cancellable,
                                                                      GAsyncReadyCallback       callback,
                                                                      gpointer                  user_data);
gboolean                sysprof_visualizers_frame_load_finish        (SysprofVisualizersFrame  *self,
                                                                      GAsyncResult             *result,
                                                                      GError                  **error);
GtkSizeGroup           *sysprof_visualizers_frame_get_size_group     (SysprofVisualizersFrame  *self);
GtkAdjustment          *sysprof_visualizers_frame_get_hadjustment    (SysprofVisualizersFrame  *self);
void                    sysprof_visualizers_frame_unselect_row       (SysprofVisualizersFrame  *self);

G_END_DECLS
