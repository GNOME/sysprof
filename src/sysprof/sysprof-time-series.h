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

GType          sysprof_time_series_get_type                  (void) G_GNUC_CONST;
SysprofSeries *sysprof_time_series_new                       (const char        *title,
                                                              GListModel        *model,
                                                              GtkExpression     *begin_time_expression,
                                                              GtkExpression     *end_time_expression,
                                                              GtkExpression     *label_expression);
GtkExpression *sysprof_time_series_get_begin_time_expression (SysprofTimeSeries *self);
void           sysprof_time_series_set_begin_time_expression (SysprofTimeSeries *self,
                                                              GtkExpression     *time_expression);
GtkExpression *sysprof_time_series_get_end_time_expression   (SysprofTimeSeries *self);
void           sysprof_time_series_set_end_time_expression   (SysprofTimeSeries *self,
                                                              GtkExpression     *end_time_expression);
GtkExpression *sysprof_time_series_get_label_expression      (SysprofTimeSeries *self);
void           sysprof_time_series_set_label_expression      (SysprofTimeSeries *self,
                                                              GtkExpression     *label_expression);
char          *sysprof_time_series_dup_label                 (SysprofTimeSeries *self,
                                                              guint              position);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofTimeSeries, g_object_unref)

G_END_DECLS
