/* sysprof-no-symbolizer.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <stdatomic.h>
#include <glib/gstdio.h>

#include "config.h"

#include "sysprof-symbolizer-private.h"
#include "sysprof-debuginfod-symbolizer.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-symbol-private.h"

// TODO: handle !HAVE_DEBUGINFOD

#include <elfutils/debuginfod.h>

struct _SysprofDebuginfodSymbolizer
{
  SysprofSymbolizer parent_instance;

  debuginfod_client *client;
  SysprofElfLoader *loader;
  GHashTable *cache;

  GCancellable *cancellable; // TODO: unused
};

struct _SysprofDebuginfodSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

static int client_progress_callback (debuginfod_client *client,
                                     long a, long b)
{
  SysprofDebuginfodSymbolizer *self = debuginfod_get_user_data (client);

  g_debug ("debuginfod progress: %ld / %ld", a, b);

  if (g_cancellable_is_cancelled (self->cancellable))
    return -1;

  return 0;
}

static gboolean initable_init (GInitable    *initable,
                               GCancellable *cancellable,
                               GError      **error)
{
  SysprofDebuginfodSymbolizer *self;

  g_return_val_if_fail (SYSPROF_IS_DEBUGINFOD_SYMBOLIZER (initable), FALSE);

  self = SYSPROF_DEBUGINFOD_SYMBOLIZER (initable);

  g_assert (!self->client);

  if (cancellable) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Cancellable initialization not supported");
    return FALSE;
  }

  self->client = debuginfod_begin ();
  if (!self->client) {
    // TODO: check if errno is really set
    g_set_error_literal (error,
                         G_IO_ERROR, g_io_error_from_errno (errno),
                         "Failed to initialize debuginfod client");
    return FALSE;
  }

  debuginfod_set_user_data (self->client, self);
  debuginfod_set_progressfn (self->client, client_progress_callback);

  self->loader = sysprof_elf_loader_new ();
  self->cache = g_hash_table_new (g_direct_hash, g_direct_equal);

  return TRUE;
}


static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = initable_init;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDebuginfodSymbolizer, sysprof_debuginfod_symbolizer, SYSPROF_TYPE_SYMBOLIZER,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

static SysprofSymbol *
sysprof_debuginfod_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                         SysprofStrings           *strings,
                                         const SysprofProcessInfo *process_info,
                                         SysprofAddressContext     context,
                                         SysprofAddress            address)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (symbolizer);
  g_autoptr (SysprofElf) elf = NULL;
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

  if (!process_info || !process_info->address_layout || !process_info->mount_namespace ||
      (context != SYSPROF_ADDRESS_CONTEXT_NONE && context != SYSPROF_ADDRESS_CONTEXT_USER))
    return NULL;

  map = sysprof_address_layout_lookup (process_info->address_layout, address);
  if (!map)
    return NULL;

  map_begin = sysprof_document_mmap_get_start_address (map);
  map_end = sysprof_document_mmap_get_end_address (map);

  g_assert (address < map_end);
  g_assert (address >= map_begin);

  file_offset = sysprof_document_mmap_get_file_offset (map);
  path = sysprof_document_mmap_get_file (map);

  elf = sysprof_elf_loader_load (self->loader,
                                 process_info->mount_namespace,
                                 path,
                                 sysprof_document_mmap_get_build_id (map),
                                 sysprof_document_mmap_get_file_inode (map),
                                 NULL);
  if (!elf)
    return NULL;

  build_id = sysprof_elf_get_build_id (elf);
  if (!build_id)
    return NULL;

  if (!g_hash_table_contains (self->cache, elf)) {
    g_autoptr (GMappedFile) mapped_file = NULL;
    g_autoptr (SysprofElf) debuginfo_elf = NULL;
    g_autofree char *debuginfo_path = NULL;
    g_autofd int fd = -1;

    fd = debuginfod_find_debuginfo (self->client,
                                    (const unsigned char *) build_id, 0,
                                    &debuginfo_path);
    if (fd < 0)
      return NULL;

    mapped_file = g_mapped_file_new_from_fd (fd, FALSE, NULL);
    if (!mapped_file)
      return NULL;

    debuginfo_elf = sysprof_elf_new (debuginfo_path, g_steal_pointer (&mapped_file), 0, NULL);
    if (!debuginfo_elf)
      return NULL;

    sysprof_elf_set_debug_link_elf (elf, debuginfo_elf);
    g_hash_table_add (self->cache, elf);
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

  g_debug ("debuginfod: 0x%" G_GINT64_MODIFIER "x: %s+0x%" G_GINT64_MODIFIER "x -> %s", address, path, relative_address, name);

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
}

static void
sysprof_debuginfod_symbolizer_finalize (GObject *object)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  g_clear_pointer (&self->cache, g_hash_table_unref);
  g_clear_object (&self->loader);
  g_clear_pointer (&self->client, debuginfod_end);
}

static void
sysprof_debuginfod_symbolizer_class_init (SysprofDebuginfodSymbolizerClass *klass)
{
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  symbolizer_class->symbolize = sysprof_debuginfod_symbolizer_symbolize;

  object_class->finalize = sysprof_debuginfod_symbolizer_finalize;
}

static void
sysprof_debuginfod_symbolizer_init (SysprofDebuginfodSymbolizer *self)
{
}

SysprofSymbolizer *
sysprof_debuginfod_symbolizer_new (GError **error)
{
  return SYSPROF_SYMBOLIZER (g_initable_new (SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, NULL, error, NULL));
}
