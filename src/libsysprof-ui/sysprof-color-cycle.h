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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_COLOR_CYCLE (sysprof_color_cycle_get_type())

typedef struct _SysprofColorCycle SysprofColorCycle;

GType              sysprof_color_cycle_get_type (void);
SysprofColorCycle *sysprof_color_cycle_ref      (SysprofColorCycle *self);
void               sysprof_color_cycle_unref    (SysprofColorCycle *self);
SysprofColorCycle *sysprof_color_cycle_new      (void);
void               sysprof_color_cycle_reset    (SysprofColorCycle *self);
void               sysprof_color_cycle_next     (SysprofColorCycle *self,
                                                 GdkRGBA           *rgba);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofColorCycle, sysprof_color_cycle_unref)

G_END_DECLS
