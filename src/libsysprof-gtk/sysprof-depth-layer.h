/* sysprof-depth-layer.h
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

#include <sysprof-analyze.h>

#include "sysprof-chart-layer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DEPTH_LAYER (sysprof_depth_layer_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDepthLayer, sysprof_depth_layer, SYSPROF, DEPTH_LAYER, SysprofChartLayer)

SYSPROF_AVAILABLE_IN_ALL
SysprofChartLayer *sysprof_depth_layer_new             (void);
SYSPROF_AVAILABLE_IN_ALL
const GdkRGBA     *sysprof_depth_layer_get_color       (SysprofDepthLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_depth_layer_set_color       (SysprofDepthLayer *self,
                                                        const GdkRGBA     *color);
SYSPROF_AVAILABLE_IN_ALL
const GdkRGBA     *sysprof_depth_layer_get_hover_color (SysprofDepthLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_depth_layer_set_hover_color (SysprofDepthLayer *self,
                                                        const GdkRGBA     *hover_color);
SYSPROF_AVAILABLE_IN_ALL
SysprofXYSeries   *sysprof_depth_layer_get_series      (SysprofDepthLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_depth_layer_set_series      (SysprofDepthLayer *self,
                                                        SysprofXYSeries   *series);

G_END_DECLS
