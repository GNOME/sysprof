/* sysprof-debuginfod-symbolizer.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <errno.h>

#include <glib/gstdio.h>

#include "sysprof-symbolizer-private.h"
#include "sysprof-debuginfod-symbolizer.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-symbol-private.h"

#if HAVE_DEBUGINFOD
# include <elfutils/debuginfod.h>
# include "sysprof-debuginfod-task-private.h"
#else
typedef struct _debuginfod_client debuginfod_client;
#endif

struct _SysprofDebuginfodSymbolizer
{
  SysprofSymbolizer  parent_instance;

  GWeakRef           loader_wr;

  debuginfod_client *client;
  SysprofElfLoader  *loader;
  GHashTable        *cache;
  GHashTable        *failed;
};

struct _SysprofDebuginfodSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

static gboolean
sysprof_debuginfod_symbolizer_initable_init (GInitable     *initable,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
#if HAVE_DEBUGINFOD
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (initable);
  debuginfod_client *client;

  /* Don't even allow creating a SysprofDebuginfodSymbolizer instance unless we
   * can create a new debuginfod_client. This ensures that even if an application
   * does `g_initable_new(SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, NULL, error, NULL)`
   * they will get `NULL` back instead of a misconfigured instance.
   */
  if (!(client = debuginfod_begin ()))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (ENOTSUP),
                           g_strerror (ENOTSUP));
      return FALSE;
    }

  self->client = client;

  return TRUE;
#else
  g_set_error_literal (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (ENOTSUP),
                       g_strerror (ENOTSUP));
  return FALSE;
#endif
}

static void
initable_init (GInitableIface *iface)
{
  iface->init = sysprof_debuginfod_symbolizer_initable_init;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDebuginfodSymbolizer, sysprof_debuginfod_symbolizer, SYSPROF_TYPE_SYMBOLIZER,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_init))

static SysprofSymbol *
sysprof_debuginfod_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                         SysprofStrings           *strings,
                                         const SysprofProcessInfo *process_info,
                                         SysprofAddressContext     context,
                                         SysprofAddress            address)
{
#if HAVE_DEBUGINFOD
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (symbolizer);
  g_autoptr(SysprofElf) elf = NULL;
  g_autofree char *name = NULL;
  SysprofSymbol *sym = NULL;
  SysprofDocumentMmap *map;
  const char *build_id;
  const char *path;
  guint64 relative_address;
  guint64 begin_address;
  guint64 end_address;
  guint64 file_offset;
  guint64 map_begin;
  guint64 map_end;

  if (process_info == NULL ||
      process_info->address_layout == NULL ||
      process_info->mount_namespace == NULL ||
      (context != SYSPROF_ADDRESS_CONTEXT_NONE && context != SYSPROF_ADDRESS_CONTEXT_USER) ||
      !(map = sysprof_address_layout_lookup (process_info->address_layout, address)))
    return NULL;

  map_begin = sysprof_document_mmap_get_start_address (map);
  map_end = sysprof_document_mmap_get_end_address (map);

  g_assert (address < map_end);
  g_assert (address >= map_begin);

  file_offset = sysprof_document_mmap_get_file_offset (map);
  path = sysprof_document_mmap_get_file (map);

  if (g_hash_table_contains (self->failed, path))
    return NULL;

  elf = sysprof_elf_loader_load (self->loader,
                                 process_info->mount_namespace,
                                 path,
                                 sysprof_document_mmap_get_build_id (map),
                                 sysprof_document_mmap_get_file_inode (map),
                                 NULL);
  if (elf == NULL)
    return NULL;

  if (!(build_id = sysprof_elf_get_build_id (elf)))
    return NULL;

  if (!g_hash_table_contains (self->cache, elf))
    {
      g_autoptr(SysprofDebuginfodTask) task = sysprof_debuginfod_task_new ();
      g_autoptr(SysprofDocumentLoader) loader = g_weak_ref_get (&self->loader_wr);
      g_autoptr(SysprofDocumentTaskScope) scope = _sysprof_document_task_register (SYSPROF_DOCUMENT_TASK (task), loader);
      g_autoptr(SysprofElf) debuginfo_elf = NULL;

      if (!(debuginfo_elf = sysprof_debuginfod_task_find_debuginfo (task, self->client, path, build_id, NULL)))
        {
          g_hash_table_insert (self->failed, g_strdup (path), NULL);
          return NULL;
        }

      sysprof_elf_set_debug_link_elf (elf, debuginfo_elf);

      g_hash_table_insert (self->cache, g_object_ref (elf), NULL);
    }

  relative_address = address;
  relative_address -= map_begin;
  relative_address += file_offset;

  name = sysprof_elf_get_symbol_at_address (elf,
                                            relative_address,
                                            &begin_address,
                                            &end_address);
  if (!name)
    return NULL;

  begin_address = CLAMP (begin_address, file_offset, file_offset + (map_end - map_begin));
  end_address = CLAMP (end_address, file_offset, file_offset + (map_end - map_begin));
  if (end_address == begin_address)
    end_address++;

  sym = _sysprof_symbol_new (sysprof_strings_get (strings, name),
                             sysprof_strings_get (strings, path),
                             sysprof_strings_get (strings, sysprof_elf_get_nick (elf)),
                             map_begin + (begin_address - file_offset),
                             map_begin + (end_address - file_offset),
                             SYSPROF_SYMBOL_KIND_USER);

  return sym;
#else
  return NULL;
#endif
}

static void
sysprof_debuginfod_symbolizer_setup (SysprofSymbolizer     *symbolizer,
                                     SysprofDocumentLoader *loader)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (symbolizer);

  g_weak_ref_set (&self->loader_wr, loader);
}

static void
sysprof_debuginfod_symbolizer_dispose (GObject *object)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  g_hash_table_remove_all (self->cache);

  g_weak_ref_set (&self->loader_wr, NULL);

  G_OBJECT_CLASS (sysprof_debuginfod_symbolizer_parent_class)->dispose (object);
}

static void
sysprof_debuginfod_symbolizer_finalize (GObject *object)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  g_clear_object (&self->loader);

  g_clear_pointer (&self->cache, g_hash_table_unref);
  g_clear_pointer (&self->failed, g_hash_table_unref);

#if HAVE_DEBUGINFOD
  g_clear_pointer (&self->client, debuginfod_end);
#endif

  g_weak_ref_clear (&self->loader_wr);

  G_OBJECT_CLASS (sysprof_debuginfod_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_debuginfod_symbolizer_class_init (SysprofDebuginfodSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->dispose = sysprof_debuginfod_symbolizer_dispose;
  object_class->finalize = sysprof_debuginfod_symbolizer_finalize;

  symbolizer_class->setup = sysprof_debuginfod_symbolizer_setup;
  symbolizer_class->symbolize = sysprof_debuginfod_symbolizer_symbolize;
}

static void
sysprof_debuginfod_symbolizer_init (SysprofDebuginfodSymbolizer *self)
{
  g_weak_ref_init (&self->loader_wr, NULL);

  self->loader = sysprof_elf_loader_new ();
  self->cache = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);
  self->failed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

SysprofSymbolizer *
sysprof_debuginfod_symbolizer_new (GError **error)
{
  SysprofSymbolizer *self;

  if (!(self = g_initable_new (SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, NULL, error, NULL)))
    return NULL;

  return self;
}
