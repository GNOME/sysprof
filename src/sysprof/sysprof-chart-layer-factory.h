/* sysprof-chart-layer-factory.h
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

#define SYSPROF_TYPE_CHART_LAYER_FACTORY (sysprof_chart_layer_factory_get_type())

G_DECLARE_FINAL_TYPE (SysprofChartLayerFactory, sysprof_chart_layer_factory, SYSPROF, CHART_LAYER_FACTORY, GObject)

SysprofChartLayerFactory *sysprof_chart_layer_factory_new_from_resource (GtkBuilderScope *scope,
                                                                         const char      *resource);
SysprofChartLayerFactory *sysprof_chart_layer_factory_new_from_bytes    (GtkBuilderScope *scope,
                                                                         GBytes          *bytes);

G_END_DECLS
