/* sysprof-kernel-symbol-resolver.c
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

#include "config.h"

#include "sysprof-kernel-symbol.h"
#include "sysprof-kernel-symbol-resolver.h"

struct _SysprofKernelSymbolResolver
{
  GObject parent_instance;
};

static GQuark linux_quark;

static gchar *
sysprof_kernel_symbol_resolver_resolve_with_context (SysprofSymbolResolver *resolver,
                                                guint64           time,
                                                GPid              pid,
                                                SysprofAddressContext  context,
                                                SysprofCaptureAddress  address,
                                                GQuark           *tag)
{
  const SysprofKernelSymbol *sym;

  g_assert (SYSPROF_IS_SYMBOL_RESOLVER (resolver));

  if (context != SYSPROF_ADDRESS_CONTEXT_KERNEL)
    return NULL;

  sym = sysprof_kernel_symbol_from_address (address);

  if (sym != NULL)
    {
      *tag = linux_quark;
      return g_strdup (sym->name);
    }

  return NULL;
}

static void
symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface)
{
  iface->resolve_with_context = sysprof_kernel_symbol_resolver_resolve_with_context;
}

G_DEFINE_TYPE_WITH_CODE (SysprofKernelSymbolResolver,
                         sysprof_kernel_symbol_resolver,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SYMBOL_RESOLVER,
                                                symbol_resolver_iface_init))

static void
sysprof_kernel_symbol_resolver_class_init (SysprofKernelSymbolResolverClass *klass)
{
  linux_quark = g_quark_from_static_string ("Kernel");
}

static void
sysprof_kernel_symbol_resolver_init (SysprofKernelSymbolResolver *skernel)
{
}

SysprofSymbolResolver *
sysprof_kernel_symbol_resolver_new (void)
{
  return g_object_new (SYSPROF_TYPE_KERNEL_SYMBOL_RESOLVER, NULL);
}
