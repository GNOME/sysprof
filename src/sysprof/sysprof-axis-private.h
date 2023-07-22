/* sysprof-axis-private.h
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

struct _SysprofAxis
{
  GObject parent_instance;
  char *title;
};

struct _SysprofAxisClass
{
  GObjectClass parent_class;

  void     (*get_min_value)   (SysprofAxis  *axis,
                               GValue       *min_value);
  double   (*normalize)       (SysprofAxis  *axis,
                               const GValue *value);
  gboolean (*is_pathological) (SysprofAxis  *axis);
};

#define SYSPROF_AXIS_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, SYSPROF_TYPE_AXIS, SysprofAxisClass)

static inline void
_sysprof_axis_get_min_value (SysprofAxis *axis,
                             GValue      *min_value)
{
  SYSPROF_AXIS_GET_CLASS (axis)->get_min_value (axis, min_value);
}

static inline double
_sysprof_axis_normalize (SysprofAxis  *axis,
                         const GValue *value)
{
  return SYSPROF_AXIS_GET_CLASS (axis)->normalize (axis, value);
}

static inline double
_sysprof_axis_is_pathological (SysprofAxis *axis)
{
  if (SYSPROF_AXIS_GET_CLASS (axis)->is_pathological)
    return SYSPROF_AXIS_GET_CLASS (axis)->is_pathological (axis);

  return FALSE;
}

void _sysprof_axis_emit_range_changed (SysprofAxis *self);

G_END_DECLS
