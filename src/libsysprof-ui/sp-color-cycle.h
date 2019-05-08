/* sp-color-cycle.h
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

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SP_TYPE_COLOR_CYCLE (sp_color_cycle_get_type())

typedef struct _SpColorCycle SpColorCycle;

SYSPROF_AVAILABLE_IN_ALL
GType         sp_color_cycle_get_type (void);
SYSPROF_AVAILABLE_IN_ALL
SpColorCycle *sp_color_cycle_ref      (SpColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
void          sp_color_cycle_unref    (SpColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
SpColorCycle *sp_color_cycle_new      (void);
SYSPROF_AVAILABLE_IN_ALL
void          sp_color_cycle_reset    (SpColorCycle *self);
SYSPROF_AVAILABLE_IN_ALL
void          sp_color_cycle_next     (SpColorCycle *self,
                                       GdkRGBA      *rgba);

G_END_DECLS
