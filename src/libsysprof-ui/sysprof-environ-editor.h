/* sysprof-environ-editor.h
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

#include "sysprof-environ.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_ENVIRON_EDITOR (sysprof_environ_editor_get_type())

G_DECLARE_FINAL_TYPE (SysprofEnvironEditor, sysprof_environ_editor, SYSPROF, ENVIRON_EDITOR, GtkListBox)

GtkWidget      *sysprof_environ_editor_new         (void);
SysprofEnviron *sysprof_environ_editor_get_environ (SysprofEnvironEditor *self);
void            sysprof_environ_editor_set_environ (SysprofEnvironEditor *self,
                                                        SysprofEnviron       *environ);

G_END_DECLS
