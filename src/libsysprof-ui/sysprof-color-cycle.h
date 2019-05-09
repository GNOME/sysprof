/* sysprof-color-cycle.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_COLOR_CYCLE (sysprof_color_cycle_get_type())

typedef struct _SysprofColorCycle SysprofColorCycle;

SYSPROF_AVAILABLE_IN_ALL
GType              sysprof_color_cycle_get_type (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofColorCycle *sysprof_color_cycle_ref      (SysprofColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_color_cycle_unref    (SysprofColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofColorCycle *sysprof_color_cycle_new      (void);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_color_cycle_reset    (SysprofColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
void               sysprof_color_cycle_next     (SysprofColorCycle *self,
                                                 GdkRGBA           *rgba);

G_END_DECLS
