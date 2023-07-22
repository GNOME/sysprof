/* sysprof-xy-layer.h
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

#include <sysprof.h>

#include "sysprof-axis.h"
#include "sysprof-chart-layer.h"
#include "sysprof-xy-series.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_XY_LAYER (sysprof_xy_layer_get_type())

G_DECLARE_FINAL_TYPE (SysprofXYLayer, sysprof_xy_layer, SYSPROF, XY_LAYER, SysprofChartLayer)

SysprofChartLayer *sysprof_xy_layer_new        (void);
gboolean           sysprof_xy_layer_get_flip_y (SysprofXYLayer  *self);
void               sysprof_xy_layer_set_flip_y (SysprofXYLayer  *self,
                                                gboolean         flip_y);
SysprofXYSeries   *sysprof_xy_layer_get_series (SysprofXYLayer  *self);
void               sysprof_xy_layer_set_series (SysprofXYLayer  *self,
                                                SysprofXYSeries *series);
SysprofAxis       *sysprof_xy_layer_get_x_axis (SysprofXYLayer  *self);
void               sysprof_xy_layer_set_x_axis (SysprofXYLayer  *self,
                                                SysprofAxis     *x_axis);
SysprofAxis       *sysprof_xy_layer_get_y_axis (SysprofXYLayer  *self);
void               sysprof_xy_layer_set_y_axis (SysprofXYLayer  *self,
                                                SysprofAxis     *y_axis);

G_END_DECLS
