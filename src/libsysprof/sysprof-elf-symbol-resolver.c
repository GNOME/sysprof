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

#include <stdio.h>
#include <string.h>

#include "binfile.h"
#include "elfparser.h"
#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-flatpak.h"
#include "sysprof-map-lookaside.h"
#include "sysprof-path-resolver.h"
#include "sysprof-podman.h"
#include "sysprof-symbol-resolver-private.h"

typedef enum
{
  PROCESS_KIND_STANDARD,
  PROCESS_KIND_FLATPAK,
  PROCESS_KIND_PODMAN,
} ProcessKind;

typedef struct
{
  char *on_host;
  char *in_process;
  int layer;
} ProcessOverlay;

typedef struct
{
  SysprofMapLookaside  *lookaside;
  SysprofPathResolver  *resolver;
  GByteArray           *mountinfo_data;
  GArray               *overlays;
  char                **debug_dirs;
  char                 *info;
  int                   pid;
  guint                 kind : 2;
} ProcessInfo;

struct _SysprofElfSymbolResolver
{
  GObject       parent_instance;

  GHashTable   *processes;
  GStringChunk *chunks;
  GHashTable   *bin_files;
  GHashTable   *tag_cache;
};

static void symbol_resolver_iface_init (SysprofSymbolResolverInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofElfSymbolResolver,
                        sysprof_elf_symbol_resolver,
                        G_TYPE_OBJECT,
                        0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SYMBOL_RESOLVER,
                                               symbol_resolver_iface_init))

static void
process_info_free (gpointer data)
{
  ProcessInfo *pi = data;

  if (pi != NULL)
    {
      g_clear_pointer (&pi->lookaside, sysprof_map_lookaside_free);
      g_clear_pointer (&pi->resolver, _sysprof_path_resolver_free);
      g_clear_pointer (&pi->mountinfo_data, g_byte_array_unref);
      g_clear_pointer (&pi->overlays, g_array_unref);
      g_clear_pointer (&pi->debug_dirs, g_strfreev);
      g_clear_pointer (&pi->info, g_free);
      g_slice_free (ProcessInfo, pi);
    }
}

static const char * const *
process_info_get_debug_dirs (const ProcessInfo *pi)
{
  static const char *standard[] = { "/usr/lib/debug", NULL };

  if (pi->kind == PROCESS_KIND_FLATPAK)
    return standard; /* TODO */
  else if (pi->kind == PROCESS_KIND_PODMAN)
    return standard; /* TODO */
  else
    return standard;
}

static void
sysprof_elf_symbol_resolver_finalize (GObject *object)
{
  SysprofElfSymbolResolver *self = (SysprofElfSymbolResolver *)object;

  g_clear_pointer (&self->bin_files, g_hash_table_unref);
  g_clear_pointer (&self->tag_cache, g_hash_table_unref);
  g_clear_pointer (&self->processes, g_hash_table_unref);
  g_clear_pointer (&self->chunks, g_string_chunk_free);

  G_OBJECT_CLASS (sysprof_elf_symbol_resolver_parent_class)->finalize (object);
}

static void
sysprof_elf_symbol_resolver_class_init (SysprofElfSymbolResolverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_elf_symbol_resolver_finalize;
}

static void
sysprof_elf_symbol_resolver_init (SysprofElfSymbolResolver *self)
{
  self->chunks = g_string_chunk_new (4096);
  self->processes = g_hash_table_new_full (NULL, NULL, NULL, process_info_free);
  self->bin_files = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           (GDestroyNotify)bin_file_free);
  self->tag_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static ProcessInfo *
sysprof_elf_symbol_resolver_get_process (SysprofElfSymbolResolver *self,
                                         int                       pid)
{
  ProcessInfo *pi;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));

  if (!(pi = g_hash_table_lookup (self->processes, GINT_TO_POINTER (pid))))
    {
      pi = g_slice_new0 (ProcessInfo);
      pi->pid = pid;
      g_hash_table_insert (self->processes, GINT_TO_POINTER (pid), pi);
    }

  return pi;
}

static void
sysprof_elf_symbol_resolver_load (SysprofSymbolResolver *resolver,
                                  SysprofCaptureReader  *reader)
{
  SysprofElfSymbolResolver *self = (SysprofElfSymbolResolver *)resolver;
  static const guint8 zero[1] = {0};
  SysprofCaptureFrameType type;
  g_autoptr(GByteArray) mounts = NULL;
  g_autofree char *mounts_data = NULL;
  GHashTableIter iter;
  ProcessInfo *pi;
  gpointer k, v;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));
  g_assert (reader != NULL);

  g_hash_table_remove_all (self->processes);

  /* First we need to load all the /proc/{pid}/mountinfo files so that
   * we can discover what files within the processes filesystem namespace
   * were mapped and where. We can use that information later to build
   * path resolvers that let us locate the files from the host.
   */
  sysprof_capture_reader_reset (reader);
  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_FILE_CHUNK)
        {
          const SysprofCaptureFileChunk *ev;
          int pid;

          if (!(ev = sysprof_capture_reader_read_file (reader)))
            break;

          pi = sysprof_elf_symbol_resolver_get_process (self, pid);

          if (strcmp (ev->path, "/.flatpak-info") == 0)
            {
              pi->kind = PROCESS_KIND_FLATPAK;
              g_free (pi->info);
              pi->info = g_strndup ((char *)ev->data, ev->len);
            }
          else if (strcmp (ev->path, "/run/.containerenv") == 0)
            {
              pi->kind = PROCESS_KIND_PODMAN;
              g_free (pi->info);
              pi->info = g_strndup ((char *)ev->data, ev->len);
            }
          else if (g_str_has_prefix (ev->path, "/proc/") &&
              g_str_has_suffix (ev->path, "/mountinfo") &&
              sscanf (ev->path, "/proc/%u/mountinfo", &pid) == 1)
            {
              if (pi->mountinfo_data == NULL)
                pi->mountinfo_data = g_byte_array_new ();
              if (ev->len)
                g_byte_array_append (pi->mountinfo_data, ev->data, ev->len);
            }
          else if (g_str_equal (ev->path, "/proc/mounts"))
            {
              if (mounts == NULL)
                mounts = g_byte_array_new ();
              if (ev->len)
                g_byte_array_append (mounts, ev->data, ev->len);
            }
        }
      else if (type == SYSPROF_CAPTURE_FRAME_OVERLAY)
        {
          const SysprofCaptureOverlay *ev;
          ProcessOverlay ov;

          if (!(ev = sysprof_capture_reader_read_overlay (reader)))
            break;

          ov.on_host = g_string_chunk_insert_const (self->chunks, ev->data);
          ov.in_process = g_string_chunk_insert_const (self->chunks, &ev->data[ev->src_len+1]);
          ov.layer = ev->layer;

          pi = sysprof_elf_symbol_resolver_get_process (self, ev->frame.pid);
          if (pi->overlays == NULL)
            pi->overlays = g_array_new (FALSE, FALSE, sizeof (ProcessOverlay));
          g_array_append_val (pi->overlays, ov);
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
        }
    }

  /* Now make sure we have access to /proc/mounts data. If we do not find it
   * within the capture, assume we're running on the same host.
   */
  if (mounts != NULL)
    {
      g_byte_array_append (mounts, zero, 1);
      mounts_data = (char *)g_byte_array_free (g_steal_pointer (&mounts), FALSE);
    }

  if (mounts_data == NULL)
    g_file_get_contents ("/proc/mounts", &mounts_data, NULL, NULL);

  /* Now that we loaded all the mountinfo data, we can create path resolvers
   * for each of the processes. Once we have that data we can walk the file
   * again to load the map events.
   */
  g_hash_table_iter_init (&iter, self->processes);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      pi = v;

      if (pi->mountinfo_data == NULL)
        continue;

      g_byte_array_append (pi->mountinfo_data, zero, 1);

      pi->resolver = _sysprof_path_resolver_new (mounts_data,
                                                 (const char *)pi->mountinfo_data->data);

      if (pi->overlays != NULL)
        {
          for (guint i = 0; i < pi->overlays->len; i++)
            {
              const ProcessOverlay *ov = &g_array_index (pi->overlays, ProcessOverlay, i);
              _sysprof_path_resolver_add_overlay (pi->resolver, ov->in_process, ov->on_host, ov->layer);
            }
        }

      if (pi->kind == PROCESS_KIND_FLATPAK)
        {
          if (pi->info != NULL)
            {
              g_autoptr(GKeyFile) keyfile = g_key_file_new ();

              if (g_key_file_load_from_data (keyfile, pi->info, (gsize)-1, 0, NULL))
                {
                  if (g_key_file_has_group (keyfile, "Instance"))
                    {
                      g_autofree gchar *app_path = g_key_file_get_string (keyfile, "Instance", "app-path", NULL);
                      g_autofree gchar *runtime_path = g_key_file_get_string (keyfile, "Instance", "runtime-path", NULL);

                      pi->debug_dirs = g_new0 (gchar *, 3);
                      pi->debug_dirs[0] = g_build_filename (app_path, "lib", "debug", NULL);
                      pi->debug_dirs[1] = g_build_filename (runtime_path, "lib", "debug", NULL);
                      pi->debug_dirs[2] = 0;

                      /* TODO: Need to locate .Debug version of runtimes. Also, extensions? */
                    }
                }
            }
        }
      else if (pi->kind == PROCESS_KIND_PODMAN)
        {
          pi->debug_dirs = g_new0 (gchar *, 2);
          pi->debug_dirs[0] = _sysprof_path_resolver_resolve (pi->resolver, "/usr/lib/debug");
          pi->debug_dirs[1] = 0;
        }
    }

  /* Walk through the file again and extract maps so long as
   * we have a resolver for them already.
   */
  sysprof_capture_reader_reset (reader);
  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_MAP)
        {
          const SysprofCaptureMap *ev = sysprof_capture_reader_read_map (reader);
          const char *filename = ev->filename;
          g_autofree char *resolved = NULL;
          SysprofMap map;

          pi = sysprof_elf_symbol_resolver_get_process (self, ev->frame.pid);

          if (pi->resolver != NULL)
            {
              resolved = _sysprof_path_resolver_resolve (pi->resolver, filename);

              if (resolved)
                filename = resolved;
            }

          map.start = ev->start;
          map.end = ev->end;
          map.offset = ev->offset;
          map.inode = ev->inode;
          map.filename = filename;

          if (pi->lookaside == NULL)
            pi->lookaside = sysprof_map_lookaside_new ();

          sysprof_map_lookaside_insert (pi->lookaside, &map);
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            return;
        }
    }
}

static bin_file_t *
sysprof_elf_symbol_resolver_get_bin_file (SysprofElfSymbolResolver *self,
                                          const ProcessInfo        *pi,
                                          const gchar              *filename)
{
  g_autofree char *alternate = NULL;
  const char * const *debug_dirs;
  g_autofree char *on_host = NULL;
  bin_file_t *bin_file;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));

  if ((bin_file = g_hash_table_lookup (self->bin_files, filename)))
    return bin_file;

  /* Debug dirs are going to be dependent on the process as different
   * containers may affect where the debug symbols are installed.
   */
  debug_dirs = process_info_get_debug_dirs (pi);
  bin_file = bin_file_new (filename, (const char * const *)debug_dirs);
  g_hash_table_insert (self->bin_files, g_strdup (filename), bin_file);

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
  const bin_symbol_t *bin_sym;
  const gchar *bin_sym_name;
  const SysprofMap *map;
  ProcessInfo *pi;
  bin_file_t *bin_file;
  gulong ubegin;
  gulong uend;

  g_assert (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self));
  g_assert (name != NULL);
  g_assert (begin != NULL);
  g_assert (end != NULL);

  *name = NULL;

  if (context != SYSPROF_ADDRESS_CONTEXT_USER)
    return FALSE;

  if (!(pi = g_hash_table_lookup (self->processes, GINT_TO_POINTER (pid))))
    return FALSE;

  map = sysprof_map_lookaside_lookup (pi->lookaside, address);
  if G_UNLIKELY (map == NULL)
    return FALSE;

  address -= map->start;
  address += map->offset;

  bin_file = sysprof_elf_symbol_resolver_get_bin_file (self, pi, map->filename);

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
  /* Do Nothing */
  /* XXX: Mark as deprecated post 41 or remove with Gtk4 port */
}

char *
_sysprof_elf_symbol_resolver_resolve_path (SysprofElfSymbolResolver *self,
                                           GPid                      pid,
                                           const char               *path)
{
  ProcessInfo *pi;

  g_return_val_if_fail (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self), NULL);

  if (!(pi = g_hash_table_lookup (self->processes, GINT_TO_POINTER (pid))))
    return NULL;

  if (pi->resolver == NULL)
    return NULL;

  return _sysprof_path_resolver_resolve (pi->resolver, path);
}

const char *
_sysprof_elf_symbol_resolver_get_pid_kind (SysprofElfSymbolResolver *self,
                                           GPid                      pid)
{
  ProcessInfo *pi;

  g_return_val_if_fail (SYSPROF_IS_ELF_SYMBOL_RESOLVER (self), NULL);

  if (!(pi = g_hash_table_lookup (self->processes, GINT_TO_POINTER (pid))))
    return "unknown";

  if (pi->kind == PROCESS_KIND_FLATPAK)
    return "Flatpak";

  if (pi->kind == PROCESS_KIND_PODMAN)
    return "Podman";

  if (pi->kind == PROCESS_KIND_STANDARD)
    return "Standard";

  return "unknown";
}
