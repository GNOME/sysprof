/*
 * sysprof-symbol.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_SYMBOL (sysprof_symbol_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSymbol, sysprof_symbol, SYSPROF, SYMBOL, GObject)

SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_symbol_get_name        (SysprofSymbol       *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_symbol_get_binary_nick (SysprofSymbol       *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_symbol_get_binary_path (SysprofSymbol       *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean    sysprof_symbol_equal           (const SysprofSymbol *a,
                                            const SysprofSymbol *b);

G_END_DECLS
