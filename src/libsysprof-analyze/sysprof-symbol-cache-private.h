/* sysprof-symbol-cache-private.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-symbol.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SYMBOL_CACHE (sysprof_symbol_cache_get_type())

G_DECLARE_FINAL_TYPE (SysprofSymbolCache, sysprof_symbol_cache, SYSPROF, SYMBOL_CACHE, GObject)

SysprofSymbolCache *sysprof_symbol_cache_new             (void);
SysprofSymbol      *sysprof_symbol_cache_lookup          (SysprofSymbolCache *self,
                                                          SysprofAddress      address);
void                sysprof_symbol_cache_take            (SysprofSymbolCache *self,
                                                          SysprofSymbol      *symbol);
void                sysprof_symbol_cache_populate_packed (SysprofSymbolCache *self,
                                                          GArray             *array,
                                                          GByteArray         *strings,
                                                          GHashTable         *strings_offset,
                                                          int                 pid);

G_END_DECLS
