/*
 * sysprof-time-span-layer.h
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

#include "sysprof-axis.h"
#include "sysprof-chart-layer.h"
#include "sysprof-time-series.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_SPAN_LAYER (sysprof_time_span_layer_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofTimeSpanLayer, sysprof_time_span_layer, SYSPROF, TIME_SPAN_LAYER, SysprofChartLayer)

SYSPROF_AVAILABLE_IN_ALL
SysprofChartLayer *sysprof_time_span_layer_new             (void);
SYSPROF_AVAILABLE_IN_ALL
const GdkRGBA     *sysprof_time_span_layer_get_color       (SysprofTimeSpanLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_time_span_layer_set_color       (SysprofTimeSpanLayer *self,
                                                            const GdkRGBA        *color);
SYSPROF_AVAILABLE_IN_ALL
const GdkRGBA     *sysprof_time_span_layer_get_event_color (SysprofTimeSpanLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_time_span_layer_set_event_color (SysprofTimeSpanLayer *self,
                                                            const GdkRGBA        *event_color);
SYSPROF_AVAILABLE_IN_ALL
SysprofTimeSeries *sysprof_time_span_layer_get_series      (SysprofTimeSpanLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_time_span_layer_set_series      (SysprofTimeSpanLayer *self,
                                                            SysprofTimeSeries    *series);
SYSPROF_AVAILABLE_IN_ALL
SysprofAxis       *sysprof_time_span_layer_get_axis        (SysprofTimeSpanLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_time_span_layer_set_axis        (SysprofTimeSpanLayer *self,
                                                            SysprofAxis          *axis);

G_END_DECLS

