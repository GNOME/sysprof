/* sp-profiler-menu-button.h
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

#include "sp-profiler.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SP_TYPE_PROFILER_MENU_BUTTON (sp_profiler_menu_button_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SpProfilerMenuButton, sp_profiler_menu_button, SP, PROFILER_MENU_BUTTON, GtkMenuButton)

struct _SpProfilerMenuButtonClass
{
  GtkMenuButtonClass parent_class;

  gpointer padding[8];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget  *sp_profiler_menu_button_new          (void);
SYSPROF_AVAILABLE_IN_ALL
void        sp_profiler_menu_button_set_profiler (SpProfilerMenuButton *self,
                                                  SpProfiler           *profiler);
SYSPROF_AVAILABLE_IN_ALL
SpProfiler *sp_profiler_menu_button_get_profiler (SpProfilerMenuButton *self);

G_END_DECLS
