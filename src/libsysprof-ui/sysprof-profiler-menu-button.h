/* sysprof-profiler-menu-button.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#include "sysprof-profiler.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROFILER_MENU_BUTTON (sysprof_profiler_menu_button_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofProfilerMenuButton, sysprof_profiler_menu_button, SYSPROF, PROFILER_MENU_BUTTON, GtkMenuButton)

struct _SysprofProfilerMenuButtonClass
{
  GtkMenuButtonClass parent_class;

  gpointer padding[8];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget  *sysprof_profiler_menu_button_new          (void);
SYSPROF_AVAILABLE_IN_ALL
void        sysprof_profiler_menu_button_set_profiler (SysprofProfilerMenuButton *self,
                                                  SysprofProfiler           *profiler);
SYSPROF_AVAILABLE_IN_ALL
SysprofProfiler *sysprof_profiler_menu_button_get_profiler (SysprofProfilerMenuButton *self);

G_END_DECLS
