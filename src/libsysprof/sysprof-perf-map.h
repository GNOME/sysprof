/* sysprof-perf-map.h
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-instrument.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PERF_MAP         (sysprof_perf_map_get_type())
#define SYSPROF_IS_PERF_MAP(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_PERF_MAP)
#define SYSPROF_PERF_MAP(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_PERF_MAP, SysprofPerfMap)
#define SYSPROF_PERF_MAP_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_PERF_MAP, SysprofPerfMapClass)

typedef struct _SysprofPerfMap      SysprofPerfMap;
typedef struct _SysprofPerfMapClass SysprofPerfMapClass;

SYSPROF_AVAILABLE_IN_ALL
GType              sysprof_perf_map_get_type (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofInstrument *sysprof_perf_map_new      (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofPerfMap, g_object_unref)

G_END_DECLS
