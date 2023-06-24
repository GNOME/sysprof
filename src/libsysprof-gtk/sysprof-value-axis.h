/* sysprof-value-axis.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_VALUE_AXIS   (sysprof_value_axis_get_type())
#define SYSPROF_IS_AXIS(obj)      (G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_AXIS))
#define SYSPROF_AXIS(obj)         (G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_AXIS, SysprofValueAxis))
#define SYSPROF_AXIS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_AXIS, SysprofValueAxisClass))

typedef struct _SysprofValueAxis SysprofValueAxis;
typedef struct _SysprofValueAxisClass SysprofValueAxisClass;

SYSPROF_AVAILABLE_IN_ALL
GType        sysprof_value_axis_get_type      (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofAxis *sysprof_value_axis_new           (double            min_value,
                                               double            max_value);
SYSPROF_AVAILABLE_IN_ALL
double       sysprof_value_axis_get_min_value (SysprofValueAxis *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_value_axis_set_min_value (SysprofValueAxis *self,
                                               double            min_value);
SYSPROF_AVAILABLE_IN_ALL
double       sysprof_value_axis_get_max_value (SysprofValueAxis *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_value_axis_set_max_value (SysprofValueAxis *self,
                                               double            max_value);

G_END_DECLS
