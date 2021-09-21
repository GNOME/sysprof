/* sysprof-symbol-resolver.c
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

#include "sysprof-platform.h"
#include "sysprof-symbol-resolver.h"

G_DEFINE_INTERFACE (SysprofSymbolResolver, sysprof_symbol_resolver, G_TYPE_OBJECT)

static gchar *
sysprof_symbol_resolver_real_resolve (SysprofSymbolResolver *self,
                                 guint64           time,
                                 GPid              pid,
                                 SysprofCaptureAddress  address,
                                 GQuark           *tag)
{
  *tag = 0;
  return NULL;
}

static gchar *
sysprof_symbol_resolver_real_resolve_with_context (SysprofSymbolResolver *self,
                                              guint64           time,
                                              GPid              pid,
                                              SysprofAddressContext  context,
                                              SysprofCaptureAddress  address,
                                              GQuark           *tag)
{
  *tag = 0;

  if (SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->resolve)
    return SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->resolve (self, time, pid, address, tag);

  return NULL;
}

static void
sysprof_symbol_resolver_default_init (SysprofSymbolResolverInterface *iface)
{
  iface->resolve = sysprof_symbol_resolver_real_resolve;
  iface->resolve_with_context = sysprof_symbol_resolver_real_resolve_with_context;
}

void
sysprof_symbol_resolver_load (SysprofSymbolResolver *self,
                         SysprofCaptureReader  *reader)
{
  g_return_if_fail (SYSPROF_IS_SYMBOL_RESOLVER (self));
  g_return_if_fail (reader != NULL);

  if (SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->load)
    SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->load (self, reader);
}

/**
 * sysprof_symbol_resolver_resolve:
 * @self: A #SysprofSymbolResolver
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
sysprof_symbol_resolver_resolve (SysprofSymbolResolver *self,
                            guint64           time,
                            GPid              pid,
                            SysprofCaptureAddress  address,
                            GQuark           *tag)
{
  GQuark dummy;

  g_return_val_if_fail (SYSPROF_IS_SYMBOL_RESOLVER (self), NULL);

  if (tag == NULL)
    tag = &dummy;

  *tag = 0;

  if (SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->resolve)
    return SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->resolve (self, time, pid, address, tag);

  return NULL;
}

/**
 * sysprof_symbol_resolver_resolve_with_context:
 * @self: A #SysprofSymbolResolver
 * @time: The time of the sample
 * @pid: The process generating the sample
 * @context: the address context
 * @address: the sample address
 * @tag: (out): A tag for the symbol.
 *
 * This function is like sysprof_symbol_resolver_resolve() but allows
 * access to the address context, which might be necessary to
 * determine the difference between user space and kernel space
 * addresses.
 *
 * Returns: (nullable) (transfer full): A newly allocated string, or %NULL.
 */
gchar *
sysprof_symbol_resolver_resolve_with_context (SysprofSymbolResolver *self,
                                              guint64                time,
                                              GPid                   pid,
                                              SysprofAddressContext  context,
                                              SysprofCaptureAddress  address,
                                              GQuark                *tag)
{
  GQuark dummy;

  g_return_val_if_fail (SYSPROF_IS_SYMBOL_RESOLVER (self), NULL);

  if (tag == NULL)
    tag = &dummy;

  *tag = 0;

  return SYSPROF_SYMBOL_RESOLVER_GET_IFACE (self)->resolve_with_context (self, time, pid, context, address, tag);
}

char *
_sysprof_symbol_resolver_load_file (SysprofCaptureReader *reader,
                                    const char           *path)
{
  g_autofree char *data = NULL;
  goffset len;
  goffset pos = 0;
  int fd;

  g_assert (reader != NULL);
  g_assert (path != NULL);

  sysprof_capture_reader_reset (reader);

  if (-1 == (fd = sysprof_memfd_create ("")) ||
      !sysprof_capture_reader_read_file_fd (reader, path, fd))
    {
      if (fd != -1)
        close (fd);
      return NULL;
    }

  len = lseek (fd, 0L, SEEK_CUR);
  data = g_malloc (len + 1);
  lseek (fd, 0L, SEEK_SET);

  while (pos < len)
    {
      gssize n_read = read (fd, data + pos, len - pos);

      if (n_read < 0)
        return NULL;

      pos += n_read;
    }

  data[len] = 0;
  close (fd);

  return g_steal_pointer (&data);
}

char **
_sysprof_symbol_resolver_guess_debug_dirs (const char *path)
{
  if (path == NULL)
    return NULL;

  return NULL;
}
