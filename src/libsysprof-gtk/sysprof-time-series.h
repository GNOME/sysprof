/* sysprof-time-series.h
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

#define SYSPROF_TYPE_TIME_SERIES         (sysprof_time_series_get_type())
#define SYSPROF_IS_TIME_SERIES(obj)      (G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_TIME_SERIES))
#define SYSPROF_TIME_SERIES(obj)         (G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_TIME_SERIES, SysprofTimeSeries))
#define SYSPROF_TIME_SERIES_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_TIME_SERIES, SysprofTimeSeriesClass))

typedef struct _SysprofTimeSeries SysprofTimeSeries;
typedef struct _SysprofTimeSeriesClass SysprofTimeSeriesClass;

SYSPROF_AVAILABLE_IN_ALL
GType          sysprof_time_series_get_type                (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofSeries *sysprof_time_series_new                     (const char      *title,
                                                            GListModel      *model,
                                                            GtkExpression   *time_expression,
                                                            GtkExpression   *duration_expression);
SYSPROF_AVAILABLE_IN_ALL
GtkExpression *sysprof_time_series_get_time_expression     (SysprofTimeSeries *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_time_series_set_time_expression     (SysprofTimeSeries *self,
                                                            GtkExpression   *x_expression);
SYSPROF_AVAILABLE_IN_ALL
GtkExpression *sysprof_time_series_get_duration_expression (SysprofTimeSeries *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_time_series_set_duration_expression (SysprofTimeSeries *self,
                                                            GtkExpression   *y_expression);

G_END_DECLS
