/* sp-elf-symbol-resolver.c
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

#include <string.h>

#include "binfile.h"
#include "elfparser.h"

#include "sp-map-lookaside.h"
#include "sp-elf-symbol-resolver.h"

typedef struct
{
  gchar *src;
  gchar *dst;
  gsize  srclen;
} SymbolDir;

struct _SpElfSymbolResolver
{
  GObject     parent_instance;

  GHashTable *lookasides;
  GHashTable *bin_files;
  GHashTable *tag_cache;
  GArray     *symbol_dirs;
};

static void symbol_resolver_iface_init (SpSymbolResolverInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpElfSymbolResolver,
                        sp_elf_symbol_resolver,
                        G_TYPE_OBJECT,
                        0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SYMBOL_RESOLVER,
                                               symbol_resolver_iface_init))

static void
sp_elf_symbol_resolver_finalize (GObject *object)
{
  SpElfSymbolResolver *self = (SpElfSymbolResolver *)object;

  g_clear_pointer (&self->bin_files, g_hash_table_unref);
  g_clear_pointer (&self->lookasides, g_hash_table_unref);
  g_clear_pointer (&self->tag_cache, g_hash_table_unref);
  g_clear_pointer (&self->symbol_dirs, g_array_unref);

  G_OBJECT_CLASS (sp_elf_symbol_resolver_parent_class)->finalize (object);
}

static void
sp_elf_symbol_resolver_class_init (SpElfSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_elf_symbol_resolver_finalize;
}

static void
sp_elf_symbol_resolver_init (SpElfSymbolResolver *self)
{
  self->symbol_dirs = g_array_new (FALSE, FALSE, sizeof (SymbolDir));

  self->lookasides = g_hash_table_new_full (NULL,
                                            NULL,
                                            NULL,
                                            (GDestroyNotify)sp_map_lookaside_free);

  self->bin_files = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           (GDestroyNotify)bin_file_free);

  self->tag_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
sp_elf_symbol_resolver_load (SpSymbolResolver *resolver,
                             SpCaptureReader  *reader)
{
  SpElfSymbolResolver *self = (SpElfSymbolResolver *)resolver;
  SpCaptureFrameType type;

  g_assert (SP_IS_SYMBOL_RESOLVER (resolver));
  g_assert (reader != NULL);

  sp_capture_reader_reset (reader);

  while (sp_capture_reader_peek_type (reader, &type))
    {
      const SpCaptureMap *ev;
      SpMapLookaside *lookaside;
      SpMap map;

      if (type != SP_CAPTURE_FRAME_MAP)
        {
          if (!sp_capture_reader_skip (reader))
            return;
          continue;
        }

      ev = sp_capture_reader_read_map (reader);

      map.start = ev->start;
      map.end = ev->end;
      map.offset = ev->offset;
      map.inode = ev->inode;
      map.filename = ev->filename;

      lookaside = g_hash_table_lookup (self->lookasides, GINT_TO_POINTER (ev->frame.pid));

      if (lookaside == NULL)
        {
          lookaside = sp_map_lookaside_new ();
          g_hash_table_insert (self->lookasides, GINT_TO_POINTER (ev->frame.pid), lookaside);
        }

      sp_map_lookaside_insert (lookaside, &map);
    }
}

static bin_file_t *
sp_elf_symbol_resolver_get_bin_file (SpElfSymbolResolver *self,
                                     const gchar         *filename)
{
  bin_file_t *bin_file;

  g_assert (SP_IS_ELF_SYMBOL_RESOLVER (self));

  if (filename == NULL)
    return NULL;

  bin_file = g_hash_table_lookup (self->bin_files, filename);

  if (bin_file == NULL)
    {
      g_autofree gchar *translated = NULL;
      const gchar *alternate = filename;

      /*
       * See if we have a symbol directory that is set to be the resolution
       * path for this filename. We will want to alter where we find the
       * symbols based on this directory.
       */
      for (guint i = 0; i < self->symbol_dirs->len; i++)
        {
          const SymbolDir *sd = &g_array_index (self->symbol_dirs, SymbolDir, i);

          if (g_str_has_prefix (filename, sd->src))
            alternate = translated = g_build_filename (sd->dst, filename + sd->srclen, NULL);
        }

      bin_file = bin_file_new (alternate);
      g_hash_table_insert (self->bin_files, g_strdup (filename), bin_file);
    }

  return bin_file;
}

static GQuark
guess_tag (SpElfSymbolResolver *self,
           const SpMap         *map)
{
  g_assert (map != NULL);
  g_assert (map->filename != NULL);

  if (!g_hash_table_contains (self->tag_cache, map->filename))
    {
      GQuark tag = 0;

      if (strstr (map->filename, "/libgobject-2.0."))
        tag = g_quark_from_static_string ("GObject");

      else if (strstr (map->filename, "/libglib-2.0."))
        tag = g_quark_from_static_string ("GLib");

      else if (strstr (map->filename, "/libgio-2.0."))
        tag = g_quark_from_static_string ("Gio");

      else if (strstr (map->filename, "/libgirepository-1.0."))
        tag = g_quark_from_static_string ("Introspection");

      else if (strstr (map->filename, "/libgtk-3."))
        tag = g_quark_from_static_string ("Gtk+");

      else if (strstr (map->filename, "/libgdk-3."))
        tag = g_quark_from_static_string ("Gdk");

      else if (strstr (map->filename, "/libgtksourceview-3.0"))
        tag = g_quark_from_static_string ("GtkSourceView");

      else if (strstr (map->filename, "/libpixman-1"))
        tag = g_quark_from_static_string ("Pixman");

      else if (strstr (map->filename, "/libcairo."))
        tag = g_quark_from_static_string ("cairo");

      else if (strstr (map->filename, "/libgstreamer-1."))
        tag = g_quark_from_static_string ("GStreamer");

      else if (strstr (map->filename, "/libX11."))
        tag = g_quark_from_static_string ("X11");

      else if (strstr (map->filename, "/libpango-1.0."))
        tag = g_quark_from_static_string ("Pango");

      else if (strstr (map->filename, "/libpangocairo-1.0."))
        tag = g_quark_from_static_string ("Pango");

      else if (strstr (map->filename, "/libpangomm-1.4."))
        tag = g_quark_from_static_string ("Pango");

      else if (strstr (map->filename, "/libpangoft2-1.0"))
        tag = g_quark_from_static_string ("Pango");

      else if (strstr (map->filename, "/libpangoxft-1.0."))
        tag = g_quark_from_static_string ("Pango");

      else if (strstr (map->filename, "/libclutter-"))
        tag = g_quark_from_static_string ("Clutter");

      else if (strstr (map->filename, "/libcogl.") ||
               strstr (map->filename, "/libcogl-"))
        tag = g_quark_from_static_string ("Cogl");

      else if (strstr (map->filename, "/libffi."))
        tag = g_quark_from_static_string ("libffi");

      else if (strstr (map->filename, "/libwayland-"))
        tag = g_quark_from_static_string ("Wayland");

      else if (strstr (map->filename, "/libinput."))
        tag = g_quark_from_static_string ("libinput");

      else if (strstr (map->filename, "/libgjs."))
        tag = g_quark_from_static_string ("Gjs");

      else if (strstr (map->filename, "/libmozjs-"))
        tag = g_quark_from_static_string ("MozJS");

      else if (strstr (map->filename, "/libGL."))
        tag = g_quark_from_static_string ("GL");

      else if (strstr (map->filename, "/libEGL."))
        tag = g_quark_from_static_string ("EGL");

      g_hash_table_insert (self->tag_cache,
                           g_strdup (map->filename),
                           GSIZE_TO_POINTER (tag));
    }

  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->tag_cache, map->filename));
}

static gchar *
sp_elf_symbol_resolver_resolve (SpSymbolResolver *resolver,
                                guint64           time,
                                GPid              pid,
                                SpCaptureAddress  address,
                                GQuark           *tag)
{
  SpElfSymbolResolver *self = (SpElfSymbolResolver *)resolver;
  const bin_symbol_t *bin_sym;
  SpMapLookaside *lookaside;
  const gchar *bin_sym_name;
  const SpMap *map;
  bin_file_t *bin_file;

  g_assert (SP_IS_ELF_SYMBOL_RESOLVER (self));

  lookaside = g_hash_table_lookup (self->lookasides, GINT_TO_POINTER (pid));
  if (lookaside == NULL)
    return NULL;

  map = sp_map_lookaside_lookup (lookaside, address);
  if (map == NULL)
    return NULL;

  address -= map->start;
  address += map->offset;

  bin_file = sp_elf_symbol_resolver_get_bin_file (self, map->filename);

  g_assert (bin_file != NULL);

  if (map->inode && !bin_file_check_inode (bin_file, map->inode))
    return g_strdup_printf ("%s: inode mismatch", map->filename);

  bin_sym = bin_file_lookup_symbol (bin_file, address);
  bin_sym_name = bin_symbol_get_name (bin_file, bin_sym);

  if (map->filename)
    *tag = guess_tag (self, map);

  return elf_demangle (bin_sym_name);
}

static void
symbol_resolver_iface_init (SpSymbolResolverInterface *iface)
{
  iface->load = sp_elf_symbol_resolver_load;
  iface->resolve = sp_elf_symbol_resolver_resolve;
}

SpSymbolResolver *
sp_elf_symbol_resolver_new (void)
{
  return g_object_new (SP_TYPE_ELF_SYMBOL_RESOLVER, NULL);
}

void
sp_elf_symbol_resolver_set_symbol_dirs (SpElfSymbolResolver *self,
                                        GHashTable          *symbol_dirs)
{
  GHashTableIter iter;

  g_return_if_fail (SP_IS_ELF_SYMBOL_RESOLVER (self));

  if (self->symbol_dirs->len)
    g_array_remove_range (self->symbol_dirs, 0, self->symbol_dirs->len - 1);

  if (symbol_dirs)
    {
      gpointer k, v;

      g_hash_table_iter_init (&iter, symbol_dirs);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          SymbolDir dir;

          dir.src = g_strdup ((gchar *)k);
          dir.srclen = strlen (dir.src);
          dir.dst = g_strdup ((gchar *)v);

          g_array_append_val (self->symbol_dirs, dir);
        }
    }
}
