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

#include <sysprof.h>

#include "sysprof-xy-layer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_LINE_LAYER (sysprof_line_layer_get_type())

G_DECLARE_FINAL_TYPE (SysprofLineLayer, sysprof_line_layer, SYSPROF, LINE_LAYER, SysprofXYLayer)

SysprofChartLayer *sysprof_line_layer_new           (void);
gboolean           sysprof_line_layer_get_antialias (SysprofLineLayer *self);
void               sysprof_line_layer_set_antialias (SysprofLineLayer *self,
                                                     gboolean          antialias);
const GdkRGBA     *sysprof_line_layer_get_color     (SysprofLineLayer *self);
void               sysprof_line_layer_set_color     (SysprofLineLayer *self,
                                                     const GdkRGBA    *color);
gboolean           sysprof_line_layer_get_dashed    (SysprofLineLayer *self);
void               sysprof_line_layer_set_dashed    (SysprofLineLayer *self,
                                                     gboolean          dashed);
gboolean           sysprof_line_layer_get_fill      (SysprofLineLayer *self);
void               sysprof_line_layer_set_fill      (SysprofLineLayer *self,
                                                     gboolean          fill);
gboolean           sysprof_line_layer_get_flip_y    (SysprofLineLayer *self);
void               sysprof_line_layer_set_flip_y    (SysprofLineLayer *self,
                                                     gboolean          flip_y);
gboolean           sysprof_line_layer_get_spline    (SysprofLineLayer *self);
void               sysprof_line_layer_set_spline    (SysprofLineLayer *self,
                                                     gboolean          spline);

G_END_DECLS
