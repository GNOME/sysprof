/* sysprof-column-layer.h
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

#include <sysprof.h>

#include "sysprof-axis.h"
#include "sysprof-xy-layer.h"
#include "sysprof-xy-series.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_COLUMN_LAYER (sysprof_column_layer_get_type())

G_DECLARE_FINAL_TYPE (SysprofColumnLayer, sysprof_column_layer, SYSPROF, COLUMN_LAYER, SysprofXYLayer)

SysprofChartLayer *sysprof_column_layer_new             (void);
const GdkRGBA     *sysprof_column_layer_get_color       (SysprofColumnLayer *self);
void               sysprof_column_layer_set_color       (SysprofColumnLayer *self,
                                                         const GdkRGBA      *color);
const GdkRGBA     *sysprof_column_layer_get_hover_color (SysprofColumnLayer *self);
void               sysprof_column_layer_set_hover_color (SysprofColumnLayer *self,
                                                         const GdkRGBA      *hover_color);

G_END_DECLS

