/*
 * sysprof-line-layer.h
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

#include <sysprof-analyze.h>

#include "sysprof-xy-layer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_LINE_LAYER (sysprof_line_layer_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofLineLayer, sysprof_line_layer, SYSPROF, LINE_LAYER, SysprofXYLayer)

SYSPROF_AVAILABLE_IN_ALL
SysprofChartLayer *sysprof_line_layer_new            (void);
SYSPROF_AVAILABLE_IN_ALL
const GdkRGBA     *sysprof_line_layer_get_color      (SysprofLineLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_line_layer_set_color      (SysprofLineLayer *self,
                                                      const GdkRGBA    *color);
SYSPROF_AVAILABLE_IN_ALL
gboolean           sysprof_line_layer_get_dashed     (SysprofLineLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_line_layer_set_dashed     (SysprofLineLayer *self,
                                                      gboolean          dashed);
SYSPROF_AVAILABLE_IN_ALL
gboolean           sysprof_line_layer_get_fill       (SysprofLineLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_line_layer_set_fill       (SysprofLineLayer *self,
                                                      gboolean          fill);
SYSPROF_AVAILABLE_IN_ALL
gboolean           sysprof_line_layer_get_flip_y     (SysprofLineLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_line_layer_set_flip_y     (SysprofLineLayer *self,
                                                      gboolean          flip_y);
SYSPROF_AVAILABLE_IN_ALL
gboolean           sysprof_line_layer_get_use_curves (SysprofLineLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_line_layer_set_use_curves (SysprofLineLayer *self,
                                                      gboolean          use_curves);

G_END_DECLS
