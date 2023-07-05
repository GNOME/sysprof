/* sysprof-xy-layer-private.h
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

#include "sysprof-normalized-series.h"
#include "sysprof-xy-layer.h"

G_BEGIN_DECLS

struct _SysprofXYLayer
{
  SysprofChartLayer        parent_instance;

  GBindingGroup           *series_bindings;
  SysprofXYSeries         *series;

  SysprofAxis             *x_axis;
  SysprofAxis             *y_axis;

  SysprofNormalizedSeries *normal_x;
  SysprofNormalizedSeries *normal_y;

  guint                    flip_y : 1;
};

struct _SysprofXYLayerClass
{
  SysprofChartLayerClass parent_instance;
};

void _sysprof_xy_layer_get_xy (SysprofXYLayer  *self,
                               const double   **x_values,
                               const double   **y_values,
                               guint           *n_values);

G_END_DECLS
