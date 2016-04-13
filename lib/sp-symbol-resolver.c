/* sp-symbol-resolver.c
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

#include "sp-symbol-resolver.h"

G_DEFINE_INTERFACE (SpSymbolResolver, sp_symbol_resolver, G_TYPE_OBJECT)

static void
sp_symbol_resolver_default_init (SpSymbolResolverInterface *iface)
{
}

void
sp_symbol_resolver_load (SpSymbolResolver *self,
                         SpCaptureReader  *reader)
{
  g_return_if_fail (SP_IS_SYMBOL_RESOLVER (self));
  g_return_if_fail (reader != NULL);

  if (SP_SYMBOL_RESOLVER_GET_IFACE (self)->load)
    SP_SYMBOL_RESOLVER_GET_IFACE (self)->load (self, reader);
}

/**
 * sp_symbol_resolver_resolve:
 * @self: A #SpSymbolResolver
 * @time: The time of the sample
 * @pid: The process generating the sample
 * @address: the sample address
 * @tag: (out): A tag for the symbol.
 *
 * Gets the symbol name for @address that was part of process @pid
 * at @time. Optionally, you can set @tag to a quark describing the
 * symbol. This can be used to provide a bit more information when
 * rendering the treeview. You might choose to describe the library
 * such as "GObject" or "GTK+" or "Linux" for the kernel.
 *
 * Returns: (nullable) (transfer full): A newly allocated string, or %NULL.
 */
gchar *
sp_symbol_resolver_resolve (SpSymbolResolver *self,
                            guint64           time,
                            GPid              pid,
                            SpCaptureAddress  address,
                            GQuark           *tag)
{
  GQuark dummy;

  g_return_val_if_fail (SP_IS_SYMBOL_RESOLVER (self), NULL);

  if (tag == NULL)
    tag = &dummy;

  *tag = 0;

  return SP_SYMBOL_RESOLVER_GET_IFACE (self)->resolve (self, time, pid, address, tag);
}
