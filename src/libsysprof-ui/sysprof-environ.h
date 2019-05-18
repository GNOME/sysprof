/* sysprof-environ.h
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

#include <gio/gio.h>

#include "sysprof-environ-variable.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_ENVIRON (sysprof_environ_get_type())

G_DECLARE_FINAL_TYPE (SysprofEnviron, sysprof_environ, SYSPROF, ENVIRON, GObject)

gboolean         ide_environ_parse           (const gchar             *pair,
                                              gchar                  **key,
                                              gchar                  **value);
SysprofEnviron  *sysprof_environ_new         (void);
void             sysprof_environ_setenv      (SysprofEnviron          *self,
                                              const gchar             *key,
                                              const gchar             *value);
const gchar     *sysprof_environ_getenv      (SysprofEnviron          *self,
                                              const gchar             *key);
gchar          **sysprof_environ_get_environ (SysprofEnviron          *self);
void             sysprof_environ_append      (SysprofEnviron          *self,
                                              SysprofEnvironVariable  *variable);
void             sysprof_environ_remove      (SysprofEnviron          *self,
                                              SysprofEnvironVariable  *variable);
SysprofEnviron  *sysprof_environ_copy        (SysprofEnviron          *self);
void             sysprof_environ_copy_into   (SysprofEnviron          *self,
                                              SysprofEnviron          *dest,
                                              gboolean                 replace);

G_END_DECLS
