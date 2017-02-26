/* sp-elf-symbol-resolver.h
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SP_ELF_SYMBOL_RESOLVER_H
#define SP_ELF_SYMBOL_RESOLVER_H

#include "sp-symbol-resolver.h"

G_BEGIN_DECLS

#define SP_TYPE_ELF_SYMBOL_RESOLVER (sp_elf_symbol_resolver_get_type())

G_DECLARE_FINAL_TYPE (SpElfSymbolResolver, sp_elf_symbol_resolver, SP, ELF_SYMBOL_RESOLVER, GObject)

SpSymbolResolver *sp_elf_symbol_resolver_new             (void);
void              sp_elf_symbol_resolver_set_symbol_dirs (SpElfSymbolResolver *self,
                                                          GHashTable          *symbol_dirs);

G_END_DECLS

#endif /* SP_ELF_SYMBOL_RESOLVER_H */
