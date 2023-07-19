/* sysprof-duplex-layer.h
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

#include "sysprof-chart-layer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DUPLEX_LAYER (sysprof_duplex_layer_get_type())

G_DECLARE_FINAL_TYPE (SysprofDuplexLayer, sysprof_duplex_layer, SYSPROF, DUPLEX_LAYER, SysprofChartLayer)

SysprofDuplexLayer *sysprof_duplex_layer_new             (void);
SysprofChartLayer  *sysprof_duplex_layer_get_upper_layer (SysprofDuplexLayer *self);
void                sysprof_duplex_layer_set_upper_layer (SysprofDuplexLayer *self,
                                                          SysprofChartLayer  *layer);
SysprofChartLayer  *sysprof_duplex_layer_get_lower_layer (SysprofDuplexLayer *self);
void                sysprof_duplex_layer_set_lower_layer (SysprofDuplexLayer *self,
                                                          SysprofChartLayer  *layer);

G_END_DECLS
