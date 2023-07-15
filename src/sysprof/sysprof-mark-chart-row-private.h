/* sysprof-mark-chart-row-private.h
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

#include "sysprof-mark-chart-item-private.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARK_CHART_ROW (sysprof_mark_chart_row_get_type())

G_DECLARE_FINAL_TYPE (SysprofMarkChartRow, sysprof_mark_chart_row, SYSPROF, MARK_CHART_ROW, GtkWidget)

SysprofMarkChartItem *sysprof_mark_chart_row_get_item (SysprofMarkChartRow  *self);
void                  sysprof_mark_chart_row_set_item (SysprofMarkChartRow  *self,
                                                       SysprofMarkChartItem *item);

G_END_DECLS
