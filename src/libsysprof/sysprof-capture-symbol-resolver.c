/* sysprof-capture-symbol-resolver.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-capture-symbol-resolver"

#include "config.h"

#include <unistd.h>

#include "sysprof-capture-symbol-resolver.h"
#include "sysprof-platform.h"
#include "sysprof-private.h"
#include "sysprof-symbol-map.h"

/**
 * SECTION:sysprof-capture-symbol-resolver:
 * @title: SysprofCaptureSymbolResolver
 * @short_description: resolve symbols from embedded data within the capture
 *
 * This looks for an embedded file "__symbols__" in the capture and tries to
 * decode them using the data stored within that file.
 *
 * This is useful when moving capture files between machines as it will allow
 * the viewer machine to get access to the symbols without having to copy them
 * to the local system.
 *
 * Since: 3.34
 */

struct _SysprofCaptureSymbolResolver
{
  GObject           parent_instance;
  SysprofSymbolMap *map;
};

static void symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofCaptureSymbolResolver, sysprof_capture_symbol_resolver, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SYMBOL_RESOLVER, symbol_resolver_iface_init))

static void
sysprof_capture_symbol_resolver_finalize (GObject *object)
{
  SysprofCaptureSymbolResolver *self = (SysprofCaptureSymbolResolver *)object;

  g_clear_pointer (&self->map, sysprof_symbol_map_free);

  G_OBJECT_CLASS (sysprof_capture_symbol_resolver_parent_class)->finalize (object);
}

static void
sysprof_capture_symbol_resolver_class_init (SysprofCaptureSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_capture_symbol_resolver_finalize;
}

static void
sysprof_capture_symbol_resolver_init (SysprofCaptureSymbolResolver *self)
{
  self->map = sysprof_symbol_map_new ();
}

SysprofSymbolResolver *
sysprof_capture_symbol_resolver_new (void)
{
  return g_object_new (SYSPROF_TYPE_CAPTURE_SYMBOL_RESOLVER, NULL);
}

static void
sysprof_capture_symbol_resolver_load (SysprofSymbolResolver *resolver,
                                      SysprofCaptureReader  *reader)
{
  SysprofCaptureSymbolResolver *self = (SysprofCaptureSymbolResolver *)resolver;
  gint byte_order;
  gint fd;

  g_assert (SYSPROF_IS_CAPTURE_SYMBOL_RESOLVER (self));
  g_assert (reader != NULL);

  byte_order = sysprof_capture_reader_get_byte_order (reader);

  if (-1 == (fd = sysprof_memfd_create ("[symbol-decoder]")))
    return;

  if (sysprof_capture_reader_read_file_fd (reader, "__symbols__", fd))
    {
      lseek (fd, 0, SEEK_SET);
      sysprof_symbol_map_deserialize (self->map, byte_order, fd);
    }

  close (fd);
}

gchar *
sysprof_capture_symbol_resolver_resolve_with_context (SysprofSymbolResolver *resolver,
                                                      guint64                time,
                                                      GPid                   pid,
                                                      SysprofAddressContext  context,
                                                      SysprofCaptureAddress  address,
                                                      GQuark                *tag)
{
  SysprofCaptureSymbolResolver *self = (SysprofCaptureSymbolResolver *)resolver;
  const gchar *name;

  g_assert (SYSPROF_IS_CAPTURE_SYMBOL_RESOLVER (self));

  if ((name = sysprof_symbol_map_lookup (self->map, time, pid, address, tag)))
    return g_strdup (name);

  return NULL;
}

static void
symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface)
{
  iface->load = sysprof_capture_symbol_resolver_load;
  iface->resolve_with_context = sysprof_capture_symbol_resolver_resolve_with_context;
}
