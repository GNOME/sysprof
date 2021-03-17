/* sysprof-elf-symbol-resolver.c
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

#include <string.h>

#include "binfile.h"
#include "elfparser.h"
#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-flatpak.h"
#include "sysprof-map-lookaside.h"
#include "sysprof-podman.h"

struct _SysprofElfSymbolResolver
{
  GObject     parent_instance;

  GArray     *debug_dirs;
  GHashTable *lookasides;
  GHashTable *bin_files;
  GHashTable *tag_cache;
};

static void symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofElfSymbolResolver,
                        sysprof_elf_symbol_resolver,
                        G_TYPE_OBJECT,
                        0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SYMBOL_RESOLVER,
                                               symbol_resolver_iface_init))

static gboolean
is_flatpak (void)
{
  static gsize did_init = FALSE;
  static gboolean ret;

  if (g_once_init_enter (&did_init))
    {
      ret = g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
      g_once_init_leave (&did_init, TRUE);
    }

  return ret;
}

static void
sysprof_elf_symbol_resolver_finalize (GObject *object)
{
  SysprofElfSymbolResolver *self = (SysprofElfSymbolResolver *)object;

  g_clear_pointer (&self->bin_files, g_hash_table_unref);
  g_clear_pointer (&self->lookasides, g_hash_table_unref);
  g_clear_pointer (&self->tag_cache, g_hash_table_unref);
  g_clear_pointer (&self->debug_dirs, g_array_unref);

  G_OBJECT_CLASS (sysprof_elf_symbol_resolver_parent_class)->finalize (object);
}

static void
sysprof_elf_symbol_resolver_class_init (SysprofElfSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_elf_symbol_resolver_finalize;
}

static void
free_element_string (gpointer data)
{
  g_free (*(gchar **)data);
}

static void
sysprof_elf_symbol_resolver_init (SysprofElfSymbolResolver *self)
{
  g_auto(GStrv) podman_dirs = NULL;

  self->debug_dirs = g_array_new (TRUE, FALSE, sizeof (gchar *));
  g_array_set_clear_func (self->debug_dirs, free_element_string);

  sysprof_elf_symbol_resolver_add_debug_dir (self, "/usr/lib/debug");
  sysprof_elf_symbol_resolver_add_debug_dir (self, "/usr/lib32/debug");
  sysprof_elf_symbol_resolver_add_debug_dir (self, "/usr/lib64/debug");

  /* The user may have podman/toolbox containers */
  podman_dirs = sysprof_podman_debug_dirs ();
  for (guint i = 0; podman_dirs[i]; i++)
    sysprof_elf_symbol_resolver_add_debug_dir (self, podman_dirs[i]);

  if (is_flatpak ())
    {
      g_auto(GStrv) debug_dirs = sysprof_flatpak_debug_dirs ();

      for (guint i = 0; debug_dirs[i]; i++)
        sysprof_elf_symbol_resolver_add_debug_dir (self, debug_dirs[i]);
    }

  self->lookasides = g_hash_table_new_full (NULL,
                                            NULL,
                                            NULL,
                                            (GDestroyNotify)sysprof_map_lookaside_free);

  self->bin_files = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           (GDestroyNotify)bin_file_free);

  self->tag_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
sysprof_elf_symbol_resolver_load (SysprofSymbolResolver *resolver,
                                  SysprofCaptureReader  *reader)
{
  SysprofElfSymbolResolver *self = (SysprofElfSymbolResolver *)resolver;
  SysprofCaptureFrameType type;

  g_assert (SYSPROF_IS_SYMBOL_RESOLVER (resolver));
  g_assert (reader != NULL);

  sysprof_capture_reader_reset (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_MAP)
        {
          const SysprofCaptureMap *ev = sysprof_capture_reader_read_map (reader);
          SysprofMapLookaside *lookaside = g_hash_table_lookup (self->lookasides, GINT_TO_POINTER (ev->frame.pid));
          const char *filename;
          SysprofMap map;

          /* Some systems using OSTree will have /sysroot/ as a prefix for
           * filenames, which we want to skip over so that we can resolve the
           * files as we see them inside the user-space view of the system.
           */

          if (strncmp (ev->filename, "/sysroot/", 9) == 0)
            filename = ev->filename + 9;
          else
            filename = ev->filename;

          map.start = ev->start;
          map.end = ev->end;
          map.offset = ev->offset;
          map.inode = ev->inode;
          map.filename = filename;

          if (lookaside == NULL)
            {
              lookaside = sysprof_map_lookaside_new ();
              g_hash_table_insert (self->lookasides, GINT_TO_POINTER (ev->frame.pid), lookaside);
            }

          sysprof_map_lookaside_insert (lookaside, &map);
        }
      else if (type == SYSPROF_CAPTURE_FRAME_OVERLAY)
        {
          const SysprofCaptureOverlay *ev = sysprof_capture_reader_read_overlay (reader);
          SysprofMapLookaside *lookaside = g_hash_table_lookup (self->lookasides, GINT_TO_POINTER (ev->frame.pid));
          const char *src = ev->data;
          const char *dst = &ev->data[ev->src_len+1];

          if (lookaside == NULL)
            {
              lookaside = sysprof_map_lookaside_new ();
              g_hash_table_insert (self->lookasides, GINT_TO_POINTER (ev->frame.pid), lookaside);
            }

          sysprof_map_lookaside_overlay (lookaside, src, dst);
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            return;
          continue;
        }
    }
}

static bin_file_t *
sysprof_elf_symbol_resolver_get_bin_file (SysprofElfSymbolResolver *self,
                                          const SysprofMapOverlay  *overlays,
                                          guint                     n_overlays,
                                          const gchar              *filename)
{
  bin_file_t *bin_file;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));

  bin_file = g_hash_table_lookup (self->bin_files, filename);

  if (bin_file == NULL)
    {
      g_autofree gchar *path = NULL;
      const gchar *alternate = filename;
      const gchar * const *dirs;

      dirs = (const gchar * const *)(gpointer)self->debug_dirs->data;

      if (overlays && filename[0] != '/' && filename[0] != '[')
        {
          for (guint i = 0; i < n_overlays; i++)
            {
              if (g_str_has_prefix (filename, overlays[i].dst+1))
                {
                  alternate = path = g_build_filename (overlays[i].src, filename, NULL);
                  break;
                }
            }
        }
      else if (is_flatpak () && g_str_has_prefix (filename, "/usr/"))
        {
          alternate = path = g_build_filename ("/var/run/host", alternate, NULL);
        }

      bin_file = bin_file_new (alternate, dirs);

      g_hash_table_insert (self->bin_files, g_strdup (filename), bin_file);
    }

  return bin_file;
}

static GQuark
guess_tag (SysprofElfSymbolResolver *self,
           const SysprofMap         *map)
{
  g_assert (map != NULL);
  g_assert (map->filename != NULL);

  if (!g_hash_table_contains (self->tag_cache, map->filename))
    {
      GQuark tag = 0;

      if (strstr (map->filename, "/libgobject-2.0."))
        tag = g_quark_from_static_string ("GObject");

      else if (strstr (map->filename, "/libc.so.6"))
        tag = g_quark_from_static_string ("libc");

      else if (strstr (map->filename, "/libstdc++.so.6"))
        tag = g_quark_from_static_string ("stdc++");

      else if (strstr (map->filename, "/libglib-2.0."))
        tag = g_quark_from_static_string ("GLib");

      else if (strstr (map->filename, "/libgio-2.0."))
        tag = g_quark_from_static_string ("Gio");

      else if (strstr (map->filename, "/libgirepository-1.0."))
        tag = g_quark_from_static_string ("Introspection");

      else if (strstr (map->filename, "/libgtk-4."))
        tag = g_quark_from_static_string ("Gtk 4");

      else if (strstr (map->filename, "/libgtk-3."))
        tag = g_quark_from_static_string ("Gtk 3");

      else if (strstr (map->filename, "/libgdk-3."))
        tag = g_quark_from_static_string ("Gdk 3");

      else if (strstr (map->filename, "/libgtksourceview-3.0"))
        tag = g_quark_from_static_string ("GtkSourceView-3");

      else if (strstr (map->filename, "/libgtksourceview-4"))
        tag = g_quark_from_static_string ("GtkSourceView-4");

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

gboolean
sysprof_elf_symbol_resolver_resolve_full (SysprofElfSymbolResolver *self,
                                          guint64                   time,
                                          GPid                      pid,
                                          SysprofAddressContext     context,
                                          SysprofCaptureAddress     address,
                                          SysprofCaptureAddress    *begin,
                                          SysprofCaptureAddress    *end,
                                          gchar                   **name,
                                          GQuark                   *tag)
{
  SysprofMapLookaside *lookaside;
  const SysprofMapOverlay *overlays = NULL;
  const bin_symbol_t *bin_sym;
  const gchar *bin_sym_name;
  const SysprofMap *map;
  bin_file_t *bin_file;
  gulong ubegin;
  gulong uend;
  guint n_overlays = 0;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));
  g_assert (name != NULL);
  g_assert (begin != NULL);
  g_assert (end != NULL);

  *name = NULL;

  if (context != SYSPROF_ADDRESS_CONTEXT_USER)
    return FALSE;

  lookaside = g_hash_table_lookup (self->lookasides, GINT_TO_POINTER (pid));
  if G_UNLIKELY (lookaside == NULL)
    return FALSE;

  map = sysprof_map_lookaside_lookup (lookaside, address);
  if G_UNLIKELY (map == NULL)
    return FALSE;

  address -= map->start;
  address += map->offset;

  if (lookaside->overlays)
    {
      overlays = &g_array_index (lookaside->overlays, SysprofMapOverlay, 0);
      n_overlays = lookaside->overlays->len;
    }

  bin_file = sysprof_elf_symbol_resolver_get_bin_file (self, overlays, n_overlays, map->filename);

  g_assert (bin_file != NULL);

  if G_UNLIKELY (map->inode && !bin_file_check_inode (bin_file, map->inode))
    {
      *name = g_strdup_printf ("%s: inode mismatch", map->filename);
      return TRUE;
    }

  bin_sym = bin_file_lookup_symbol (bin_file, address);
  bin_sym_name = bin_symbol_get_name (bin_file, bin_sym);

  if G_LIKELY (map->filename)
    *tag = guess_tag (self, map);

  *name = elf_demangle (bin_sym_name);
  bin_symbol_get_address_range (bin_file, bin_sym, &ubegin, &uend);

  *begin = ubegin;
  *end = uend;

  return TRUE;
}

static gchar *
sysprof_elf_symbol_resolver_resolve_with_context (SysprofSymbolResolver *resolver,
                                                  guint64                time,
                                                  GPid                   pid,
                                                  SysprofAddressContext  context,
                                                  SysprofCaptureAddress  address,
                                                  GQuark                *tag)
{
  gchar *name = NULL;
  SysprofCaptureAddress begin, end;

  /* If not user context, nothing we can do here */
  if (context != SYSPROF_ADDRESS_CONTEXT_USER)
    return NULL;

  /* If this is a jitmap entry, bail early to save some cycles */
  if ((address & SYSPROF_CAPTURE_JITMAP_MARK) == SYSPROF_CAPTURE_JITMAP_MARK)
    return NULL;

  sysprof_elf_symbol_resolver_resolve_full (SYSPROF_ELF_SYMBOL_RESOLVER (resolver),
                                            time,
                                            pid,
                                            context,
                                            address,
                                            &begin,
                                            &end,
                                            &name,
                                            tag);

  return g_steal_pointer (&name);
}

static void
symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface)
{
  iface->load = sysprof_elf_symbol_resolver_load;
  iface->resolve_with_context = sysprof_elf_symbol_resolver_resolve_with_context;
}

SysprofSymbolResolver *
sysprof_elf_symbol_resolver_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_SYMBOL_RESOLVER, NULL);
}

void
sysprof_elf_symbol_resolver_add_debug_dir (SysprofElfSymbolResolver *self,
                                           const gchar              *debug_dir)
{
  gchar *val;

  g_return_if_fail (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));
  g_return_if_fail (debug_dir != NULL);

  if (!g_file_test (debug_dir, G_FILE_TEST_EXISTS))
    return;

  for (guint i = 0; i < self->debug_dirs->len; i++)
    {
      gchar * const *str = &g_array_index (self->debug_dirs, gchar *, i);

      if (g_strcmp0 (*str, debug_dir) == 0)
        return;
    }

  val = g_strdup (debug_dir);
  g_array_append_val (self->debug_dirs, val);
}
