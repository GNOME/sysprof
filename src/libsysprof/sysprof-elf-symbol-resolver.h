/* sysprof-elf-symbol-resolver.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-symbol-resolver.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_ELF_SYMBOL_RESOLVER (sysprof_elf_symbol_resolver_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofElfSymbolResolver, sysprof_elf_symbol_resolver, SYSPROF, ELF_SYMBOL_RESOLVER, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSymbolResolver *sysprof_elf_symbol_resolver_new          (void);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_elf_symbol_resolver_add_debug_dir (SysprofElfSymbolResolver  *self,
                                                                  const gchar               *debug_dir);
SYSPROF_AVAILABLE_IN_ALL
gboolean               sysprof_elf_symbol_resolver_resolve_full  (SysprofElfSymbolResolver  *self,
                                                                  guint64                    time,
                                                                  GPid                       pid,
                                                                  SysprofAddressContext      context,
                                                                  SysprofCaptureAddress      address,
                                                                  SysprofCaptureAddress     *begin,
                                                                  SysprofCaptureAddress     *end,
                                                                  gchar                    **name,
                                                                  GQuark                    *tag);

G_END_DECLS
