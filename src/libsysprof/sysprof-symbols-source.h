/* sysprof-symbols-source.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-source.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SYMBOLS_SOURCE (sysprof_symbols_source_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSymbolsSource, sysprof_symbols_source, SYSPROF, SYMBOLS_SOURCE, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSource *sysprof_symbols_source_new           (void);
SYSPROF_AVAILABLE_IN_3_36
void           sysprof_symbols_source_set_user_only (SysprofSymbolsSource *self,
                                                     gboolean              user_only);
SYSPROF_AVAILABLE_IN_3_36
gboolean       sysprof_symbols_source_get_user_only (SysprofSymbolsSource *self);

G_END_DECLS
