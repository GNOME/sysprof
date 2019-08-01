/* sysprof-visualizers-group-private.h
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

#include <sysprof-capture.h>

#include "sysprof-visualizer-group.h"
#include "sysprof-visualizer-group-header.h"

G_BEGIN_DECLS

void                          _sysprof_visualizer_group_set_reader        (SysprofVisualizerGroup       *self,
                                                                           SysprofCaptureReader         *reader);
SysprofVisualizerGroupHeader *_sysprof_visualizer_group_header_new        (void);
void                          _sysprof_visualizer_group_header_add_row    (SysprofVisualizerGroupHeader *self,
                                                                           guint                         position,
                                                                           const gchar                  *title,
                                                                           GMenuModel                   *menu,
                                                                           GtkWidget                    *row);
void                          _sysprof_visualizer_group_header_remove_row (SysprofVisualizerGroupHeader *self,
                                                                           guint                         row);
void                          _sysprof_visualizer_group_set_header        (SysprofVisualizerGroup       *self,
                                                                           SysprofVisualizerGroupHeader *header);


G_END_DECLS
