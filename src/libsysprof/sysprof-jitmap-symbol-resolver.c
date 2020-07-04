/* sysprof-jitmap-symbol-resolver.c
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
#include "sysprof-jitmap-symbol-resolver.h"

struct _SysprofJitmapSymbolResolver
{
  GObject     parent_instance;
  GHashTable *jitmap;
};

static void symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofJitmapSymbolResolver,
                        sysprof_jitmap_symbol_resolver,
                        G_TYPE_OBJECT,
                        0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SYMBOL_RESOLVER,
                                               symbol_resolver_iface_init))

static void
sysprof_jitmap_symbol_resolver_finalize (GObject *object)
{
  SysprofJitmapSymbolResolver *self = (SysprofJitmapSymbolResolver *)object;

  g_clear_pointer (&self->jitmap, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_jitmap_symbol_resolver_parent_class)->finalize (object);
}

static void
sysprof_jitmap_symbol_resolver_class_init (SysprofJitmapSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_jitmap_symbol_resolver_finalize;
}

static void
sysprof_jitmap_symbol_resolver_init (SysprofJitmapSymbolResolver *self)
{
  self->jitmap = g_hash_table_new_full (NULL, NULL, NULL, g_free);
}

static void
sysprof_jitmap_symbol_resolver_load (SysprofSymbolResolver *resolver,
                                     SysprofCaptureReader  *reader)
{
  SysprofJitmapSymbolResolver *self = (SysprofJitmapSymbolResolver *)resolver;
  SysprofCaptureFrameType type;

  g_assert (SYSPROF_IS_JITMAP_SYMBOL_RESOLVER (self));
  g_assert (reader != NULL);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      const SysprofCaptureJitmap *jitmap;
      SysprofCaptureJitmapIter iter;
      SysprofCaptureAddress addr;
      const gchar *str;

      if (type != SYSPROF_CAPTURE_FRAME_JITMAP)
        {
          if (!sysprof_capture_reader_skip (reader))
            return;
          continue;
        }

      if (!(jitmap = sysprof_capture_reader_read_jitmap (reader)))
        return;

      sysprof_capture_jitmap_iter_init (&iter, jitmap);
      while (sysprof_capture_jitmap_iter_next (&iter, &addr, &str))
        g_hash_table_insert (self->jitmap, GSIZE_TO_POINTER (addr), g_strdup (str));
    }
}

static gchar *
sysprof_jitmap_symbol_resolver_resolve (SysprofSymbolResolver *resolver,
                                        guint64                time,
                                        GPid                   pid,
                                        SysprofCaptureAddress  address,
                                        GQuark                *tag)
{
  SysprofJitmapSymbolResolver *self = (SysprofJitmapSymbolResolver *)resolver;

  g_assert (SYSPROF_IS_JITMAP_SYMBOL_RESOLVER (self));

  *tag = 0;

  return g_strdup (g_hash_table_lookup (self->jitmap, GSIZE_TO_POINTER (address)));
}

static void
symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface)
{
  iface->load = sysprof_jitmap_symbol_resolver_load;
  iface->resolve = sysprof_jitmap_symbol_resolver_resolve;
}

SysprofSymbolResolver *
sysprof_jitmap_symbol_resolver_new (void)
{
  return g_object_new (SYSPROF_TYPE_JITMAP_SYMBOL_RESOLVER, NULL);
}
