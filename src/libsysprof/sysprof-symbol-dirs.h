/* sysprof-symbol-dirs.h
 *
 * Copyright 2017-2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include "sysprof-version-macros.h"

#include <glib.h>

G_BEGIN_DECLS

SYSPROF_AVAILABLE_IN_ALL
void    sysprof_symbol_dirs_add       (const gchar *dir);
SYSPROF_AVAILABLE_IN_ALL
void    sysprof_symbol_dirs_remove    (const gchar *dir);
SYSPROF_AVAILABLE_IN_ALL
gchar **sysprof_symbol_dirs_get_paths (const gchar *dir,
                                  const gchar *name);

G_END_DECLS
