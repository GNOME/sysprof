/* sysprof-screencast.h
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

#define SYSPROF_TYPE_SCREENCAST (sysprof_screencast_get_type())
#define SYSPROF_IS_SCREENCAST(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_SCREENCAST)
#define SYSPROF_SCREENCAST(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_SCREENCAST, SysprofScreencast)

typedef struct _SysprofScreencast      SysprofScreencast;
typedef struct _SysprofScreencastClass SysprofScreencastClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofScreencast, g_object_unref)

SYSPROF_AVAILABLE_IN_52
GType              sysprof_screencast_get_type     (void);
SYSPROF_AVAILABLE_IN_52
SysprofInstrument *sysprof_screencast_new          (void);
SYSPROF_AVAILABLE_IN_52
SysprofInstrument *sysprof_screencast_new_for_area (int x,
                                                    int y,
                                                    int width,
                                                    int height);

G_END_DECLS
