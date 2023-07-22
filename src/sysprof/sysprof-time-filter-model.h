/* sysprof-time-filter-model.h
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

#include <gio/gio.h>

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_FILTER_MODEL (sysprof_time_filter_model_get_type())

G_DECLARE_FINAL_TYPE (SysprofTimeFilterModel, sysprof_time_filter_model, SYSPROF, TIME_FILTER_MODEL, GObject)

SysprofTimeFilterModel *sysprof_time_filter_model_new            (GListModel             *model,
                                                                  const SysprofTimeSpan  *time_span);
GListModel             *sysprof_time_filter_model_get_model      (SysprofTimeFilterModel *self);
void                    sysprof_time_filter_model_set_model      (SysprofTimeFilterModel *self,
                                                                  GListModel             *model);
GtkExpression          *sysprof_time_filter_model_get_expression (SysprofTimeFilterModel *self);
void                    sysprof_time_filter_model_set_expression (SysprofTimeFilterModel *self,
                                                                  GtkExpression          *expression);
const SysprofTimeSpan  *sysprof_time_filter_model_get_time_span  (SysprofTimeFilterModel *self);
void                    sysprof_time_filter_model_set_time_span  (SysprofTimeFilterModel *self,
                                                                  const SysprofTimeSpan  *time_span);

G_END_DECLS
