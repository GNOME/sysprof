/* sysprof-elf-symbol-resolver-private.h
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-elf-symbol-resolver.h"

G_BEGIN_DECLS

char       *_sysprof_elf_symbol_resolver_resolve_path (SysprofElfSymbolResolver *self,
                                                       GPid                      pid,
                                                       const char               *path);
const char *_sysprof_elf_symbol_resolver_get_pid_kind (SysprofElfSymbolResolver *self,
                                                       GPid                      pid);

G_END_DECLS
