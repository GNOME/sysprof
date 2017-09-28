/* sp-symbol-dirs.h
 *
 * Copyright (C) 2017 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_SYMBOL_DIRS_H
#define SP_SYMBOL_DIRS_H

#include <glib.h>

G_BEGIN_DECLS

void    sp_symbol_dirs_add       (const gchar *dir);
void    sp_symbol_dirs_remove    (const gchar *dir);
gchar **sp_symbol_dirs_get_paths (const gchar *dir,
                                  const gchar *name);

G_END_DECLS

#endif /* SP_SYMBOL_DIRS_H */
