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

#define SYSPROF_TYPE_XY_SERIES         (sysprof_xy_series_get_type())
#define SYSPROF_IS_XY_SERIES(obj)      (G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_XY_SERIES))
#define SYSPROF_XY_SERIES(obj)         (G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_XY_SERIES, SysprofXYSeries))
#define SYSPROF_XY_SERIES_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_XY_SERIES, SysprofXYSeriesClass))

typedef struct _SysprofXYSeries SysprofXYSeries;
typedef struct _SysprofXYSeriesClass SysprofXYSeriesClass;

GType          sysprof_xy_series_get_type         (void) G_GNUC_CONST;
SysprofSeries *sysprof_xy_series_new              (const char      *title,
                                                   GListModel      *model,
                                                   GtkExpression   *x_expression,
                                                   GtkExpression   *y_expression);
GtkExpression *sysprof_xy_series_get_x_expression (SysprofXYSeries *self);
void           sysprof_xy_series_set_x_expression (SysprofXYSeries *self,
                                                   GtkExpression   *x_expression);
GtkExpression *sysprof_xy_series_get_y_expression (SysprofXYSeries *self);
void           sysprof_xy_series_set_y_expression (SysprofXYSeries *self,
                                                   GtkExpression   *y_expression);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofXYSeries, g_object_unref)

G_END_DECLS
