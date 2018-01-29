/* sp-kernel-symbol-resolver.c
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

#include "symbols/sp-kernel-symbol.h"
#include "symbols/sp-kernel-symbol-resolver.h"

struct _SpKernelSymbolResolver
{
  GObject parent_instance;
};

static GQuark linux_quark;

static gchar *
sp_kernel_symbol_resolver_resolve_with_context (SpSymbolResolver *resolver,
                                                guint64           time,
                                                GPid              pid,
                                                SpAddressContext  context,
                                                SpCaptureAddress  address,
                                                GQuark           *tag)
{
  const SpKernelSymbol *sym;

  g_assert (SP_IS_SYMBOL_RESOLVER (resolver));

  if (context != SP_ADDRESS_CONTEXT_KERNEL)
    return NULL;

  if (NULL != (sym = sp_kernel_symbol_from_address (address)))
    {
      *tag = linux_quark;
      return g_strdup (sym->name);
    }

  return NULL;
}

static void
symbol_resolver_iface_init (SpSymbolResolverInterface *iface)
{
  iface->resolve_with_context = sp_kernel_symbol_resolver_resolve_with_context;
}

G_DEFINE_TYPE_WITH_CODE (SpKernelSymbolResolver,
                         sp_kernel_symbol_resolver,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SP_TYPE_SYMBOL_RESOLVER,
                                                symbol_resolver_iface_init))

static void
sp_kernel_symbol_resolver_class_init (SpKernelSymbolResolverClass *klass)
{
  linux_quark = g_quark_from_static_string ("Linux");
}

static void
sp_kernel_symbol_resolver_init (SpKernelSymbolResolver *skernel)
{
}

SpSymbolResolver *
sp_kernel_symbol_resolver_new (void)
{
  return g_object_new (SP_TYPE_KERNEL_SYMBOL_RESOLVER, NULL);
}
