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

#define G_LOG_DOMAIN "sysprof-kernel-symbol-resolver"

#include "config.h"

#include <unistd.h>

#include "sysprof-kallsyms.h"
#include "sysprof-kernel-symbol.h"
#include "sysprof-kernel-symbol-resolver.h"
#include "sysprof-private.h"

#include "sysprof-platform.h"

struct _SysprofKernelSymbolResolver
{
  GObject               parent_instance;
  SysprofKernelSymbols *symbols;
};

static GQuark linux_quark;

static gchar *
sysprof_kernel_symbol_resolver_resolve_with_context (SysprofSymbolResolver *resolver,
                                                     guint64                time,
                                                     GPid                   pid,
                                                     SysprofAddressContext  context,
                                                     SysprofCaptureAddress  address,
                                                     GQuark                *tag)
{
  SysprofKernelSymbolResolver *self = (SysprofKernelSymbolResolver *)resolver;
  const SysprofKernelSymbol *sym;

  g_assert (SYSPROF_IS_SYMBOL_RESOLVER (self));
  g_assert (tag != NULL);

  if (context != SYSPROF_ADDRESS_CONTEXT_KERNEL)
    return NULL;

  if (self->symbols == NULL)
    return NULL;

  if ((sym = _sysprof_kernel_symbols_lookup (self->symbols, address)))
    {
      *tag = linux_quark;
      return g_strdup (sym->name);
    }

  return NULL;
}

static void
sysprof_kernel_symbol_resolver_load (SysprofSymbolResolver *resolver,
                                     SysprofCaptureReader  *reader)
{
  static const guint8 zero[] = {0};
  SysprofKernelSymbolResolver *self = (SysprofKernelSymbolResolver *)resolver;
  g_autoptr(GByteArray) bytes = NULL;
  g_autoptr(SysprofKallsyms) kallsyms = NULL;
  guint8 buf[4096];
  gint data_fd;

  g_assert (SYSPROF_IS_KERNEL_SYMBOL_RESOLVER (self));
  g_assert (reader != NULL);

  /* If there is an embedded __symbols__ file, then we won't do anything
   * because we want to use the symbols from the peer.
   */
  if (sysprof_capture_reader_find_file (reader, "__symbols__"))
    return;

  sysprof_capture_reader_reset (reader);

  if (-1 == (data_fd = sysprof_memfd_create ("[sysprof-kallsyms]")) ||
      !sysprof_capture_reader_read_file_fd (reader, "/proc/kallsyms", data_fd))
    {
      if (data_fd != -1)
        close (data_fd);
      self->symbols = _sysprof_kernel_symbols_get_shared ();
      return;
    }

  bytes = g_byte_array_new ();
  lseek (data_fd, 0, SEEK_SET);

  for (;;)
    {
      gssize len = read (data_fd, buf, sizeof buf);

      if (len <= 0)
        break;

      g_byte_array_append (bytes, buf, len);
    }

  g_byte_array_append (bytes, zero, 1);

  if (bytes->len > 1)
    {
      kallsyms = sysprof_kallsyms_new_take ((gchar *)g_byte_array_free (g_steal_pointer (&bytes), FALSE));
      self->symbols = _sysprof_kernel_symbols_new_from_kallsyms (kallsyms);
    }
  else
    {
      self->symbols = _sysprof_kernel_symbols_get_shared ();
    }
}

static void
symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface)
{
  iface->load = sysprof_kernel_symbol_resolver_load;
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
