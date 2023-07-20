/* sysprof-callgraph-symbol.h
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

#include <sysprof-capture.h>

#include "sysprof-symbol.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_CALLGRAPH_SYMBOL (sysprof_callgraph_symbol_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofCallgraphSymbol, sysprof_callgraph_symbol, SYSPROF, CALLGRAPH_SYMBOL, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSymbol *sysprof_callgraph_symbol_get_symbol          (SysprofCallgraphSymbol *self);
SYSPROF_AVAILABLE_IN_ALL
gpointer       sysprof_callgraph_symbol_get_summary_augment (SysprofCallgraphSymbol *self);

G_END_DECLS
