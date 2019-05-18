/* sysprof-environ-variable.h
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

#include <glib-object.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_ENVIRON_VARIABLE (sysprof_environ_variable_get_type())

G_DECLARE_FINAL_TYPE (SysprofEnvironVariable, sysprof_environ_variable, SYSPROF, ENVIRON_VARIABLE, GObject)

SysprofEnvironVariable *sysprof_environ_variable_new       (const gchar            *key,
                                                            const gchar            *value);
const gchar            *sysprof_environ_variable_get_key   (SysprofEnvironVariable *self);
void                    sysprof_environ_variable_set_key   (SysprofEnvironVariable *self,
                                                            const gchar            *key);
const gchar            *sysprof_environ_variable_get_value (SysprofEnvironVariable *self);
void                    sysprof_environ_variable_set_value (SysprofEnvironVariable *self,
                                                            const gchar            *value);

G_END_DECLS
