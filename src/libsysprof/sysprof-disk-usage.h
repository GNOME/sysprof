/* sysprof-disk-usage.h
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

#include "sysprof-instrument.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DISK_USAGE         (sysprof_disk_usage_get_type())
#define SYSPROF_IS_DISK_USAGE(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DISK_USAGE)
#define SYSPROF_DISK_USAGE(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DISK_USAGE, SysprofDiskUsage)
#define SYSPROF_DISK_USAGE_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DISK_USAGE, SysprofDiskUsageClass)

typedef struct _SysprofDiskUsage      SysprofDiskUsage;
typedef struct _SysprofDiskUsageClass SysprofDiskUsageClass;

SYSPROF_AVAILABLE_IN_ALL
GType              sysprof_disk_usage_get_type (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofInstrument *sysprof_disk_usage_new      (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDiskUsage, g_object_unref)

G_END_DECLS
