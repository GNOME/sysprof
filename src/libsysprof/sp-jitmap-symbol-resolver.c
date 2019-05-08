/* sp-jitmap-symbol-resolver.c
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

#include "sp-kernel-symbol.h"
#include "sp-jitmap-symbol-resolver.h"

struct _SpJitmapSymbolResolver
{
  GObject     parent_instance;
  GHashTable *jitmap;
};

static void symbol_resolver_iface_init (SpSymbolResolverInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpJitmapSymbolResolver,
                        sp_jitmap_symbol_resolver,
                        G_TYPE_OBJECT,
                        0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SYMBOL_RESOLVER,
                                               symbol_resolver_iface_init))

static void
sp_jitmap_symbol_resolver_finalize (GObject *object)
{
  SpJitmapSymbolResolver *self = (SpJitmapSymbolResolver *)object;

  g_clear_pointer (&self->jitmap, g_hash_table_unref);

  G_OBJECT_CLASS (sp_jitmap_symbol_resolver_parent_class)->finalize (object);
}

static void
sp_jitmap_symbol_resolver_class_init (SpJitmapSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_jitmap_symbol_resolver_finalize;
}

static void
sp_jitmap_symbol_resolver_init (SpJitmapSymbolResolver *self)
{
  self->jitmap = g_hash_table_new_full (NULL, NULL, NULL, g_free);
}

static void
sp_jitmap_symbol_resolver_load (SpSymbolResolver *resolver,
                                SpCaptureReader  *reader)
{
  SpJitmapSymbolResolver *self = (SpJitmapSymbolResolver *)resolver;
  SpCaptureFrameType type;

  g_assert (SP_IS_JITMAP_SYMBOL_RESOLVER (self));
  g_assert (reader != NULL);

  while (sp_capture_reader_peek_type (reader, &type))
    {
      g_autoptr(GHashTable) jitmap = NULL;
      GHashTableIter iter;
      SpCaptureAddress addr;
      const gchar *str;

      if (type != SP_CAPTURE_FRAME_JITMAP)
        {
          if (!sp_capture_reader_skip (reader))
            return;
          continue;
        }

      if (!(jitmap = sp_capture_reader_read_jitmap (reader)))
        return;

      g_hash_table_iter_init (&iter, jitmap);
      while (g_hash_table_iter_next (&iter, (gpointer *)&addr, (gpointer *)&str))
        g_hash_table_insert (self->jitmap, GSIZE_TO_POINTER (addr), g_strdup (str));
    }
}

static gchar *
sp_jitmap_symbol_resolver_resolve (SpSymbolResolver *resolver,
                                   guint64           time,
                                   GPid              pid,
                                   SpCaptureAddress  address,
                                   GQuark           *tag)
{
  SpJitmapSymbolResolver *self = (SpJitmapSymbolResolver *)resolver;

  g_assert (SP_IS_JITMAP_SYMBOL_RESOLVER (self));

  *tag = 0;

  return g_strdup (g_hash_table_lookup (self->jitmap, GSIZE_TO_POINTER (address)));
}

static void
symbol_resolver_iface_init (SpSymbolResolverInterface *iface)
{
  iface->load = sp_jitmap_symbol_resolver_load;
  iface->resolve = sp_jitmap_symbol_resolver_resolve;
}

SpSymbolResolver *
sp_jitmap_symbol_resolver_new (void)
{
  return g_object_new (SP_TYPE_JITMAP_SYMBOL_RESOLVER, NULL);
}
