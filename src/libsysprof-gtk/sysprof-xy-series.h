/* sysprof-xy-series.h
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

#include "sysprof-series.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_XY_SERIES (sysprof_xy_series_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofXYSeries, sysprof_xy_series, SYSPROF, XY_SERIES, SysprofSeries)

SYSPROF_AVAILABLE_IN_ALL
SysprofSeries *sysprof_xy_series_new              (const char      *title,
                                                   GListModel      *model,
                                                   GtkExpression   *x_expression,
                                                   GtkExpression   *y_expression);
SYSPROF_AVAILABLE_IN_ALL
GtkExpression *sysprof_xy_series_get_x_expression (SysprofXYSeries *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_xy_series_set_x_expression (SysprofXYSeries *self,
                                                   GtkExpression   *x_expression);
SYSPROF_AVAILABLE_IN_ALL
GtkExpression *sysprof_xy_series_get_y_expression (SysprofXYSeries *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_xy_series_set_y_expression (SysprofXYSeries *self,
                                                   GtkExpression   *y_expression);

G_END_DECLS
