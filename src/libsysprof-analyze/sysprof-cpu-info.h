/* sysprof-cpu-info.h
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

#include <glib-object.h>

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_CPU_INFO (sysprof_cpu_info_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofCpuInfo, sysprof_cpu_info, SYSPROF, CPU_INFO, GObject)

SYSPROF_AVAILABLE_IN_ALL
guint       sysprof_cpu_info_get_id         (SysprofCpuInfo *self);
SYSPROF_AVAILABLE_IN_ALL
guint       sysprof_cpu_info_get_core_id    (SysprofCpuInfo *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_cpu_info_get_model_name (SysprofCpuInfo *self);

G_END_DECLS
