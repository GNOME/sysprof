/* sp-symbol-resolver.h
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_SYMBOL_RESOLVER_H
#define SP_SYMBOL_RESOLVER_H

#include <glib-object.h>

#include "sp-address.h"
#include "sp-capture-reader.h"

G_BEGIN_DECLS

#define SP_TYPE_SYMBOL_RESOLVER (sp_symbol_resolver_get_type())

G_DECLARE_INTERFACE (SpSymbolResolver, sp_symbol_resolver, SP, SYMBOL_RESOLVER, GObject)

struct _SpSymbolResolverInterface
{
  GTypeInterface parent_interface;

  void   (*load)                 (SpSymbolResolver *self,
                                  SpCaptureReader  *reader);
  gchar *(*resolve)              (SpSymbolResolver *self,
                                  guint64           time,
                                  GPid              pid,
                                  SpCaptureAddress  address,
                                  GQuark           *tag);
  gchar *(*resolve_with_context) (SpSymbolResolver *self,
                                  guint64           time,
                                  GPid              pid,
                                  SpAddressContext  context,
                                  SpCaptureAddress  address,
                                  GQuark           *tag);
};

void   sp_symbol_resolver_load                 (SpSymbolResolver *self,
                                                SpCaptureReader  *reader);
gchar *sp_symbol_resolver_resolve              (SpSymbolResolver *self,
                                                guint64           time,
                                                GPid              pid,
                                                SpCaptureAddress  address,
                                                GQuark           *tag);
gchar *sp_symbol_resolver_resolve_with_context (SpSymbolResolver *self,
                                                guint64           time,
                                                GPid              pid,
                                                SpAddressContext  context,
                                                SpCaptureAddress  address,
                                                GQuark           *tag);

G_END_DECLS

#endif /* SP_SYMBOL_RESOLVER_H */
