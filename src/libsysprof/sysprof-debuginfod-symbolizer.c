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

#include "config.h"

#include <elfutils/debuginfod.h>
#include <errno.h>
#include <stdatomic.h>

#include <glib/gstdio.h>

#include "sysprof-symbolizer-private.h"
#include "sysprof-debuginfod-symbolizer.h"
#include "sysprof-debuginfod-task-private.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofDebuginfodSymbolizer
{
  SysprofSymbolizer  parent_instance;

  debuginfod_client *client;
  SysprofElfLoader  *loader;
  GHashTable        *cache;

  double             progress;

  guint              cancelled : 1;
};

struct _SysprofDebuginfodSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

enum {
  PROP_0,
  PROP_PROGRESS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static int
client_progress_callback (debuginfod_client *client,
                          long               a,
                          long               b)
{
  SysprofDebuginfodSymbolizer *self = debuginfod_get_user_data (client);
  double progress = b > 0 ? (double)a / (double)b : 0;

  g_debug ("debuginfod progress: %lf\n", progress);

  return self->cancelled ? -1 : 0;
}

G_DEFINE_FINAL_TYPE (SysprofDebuginfodSymbolizer, sysprof_debuginfod_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static SysprofSymbol *
sysprof_debuginfod_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                         SysprofStrings           *strings,
                                         const SysprofProcessInfo *process_info,
                                         SysprofAddressContext     context,
                                         SysprofAddress            address)
{
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
      g_autoptr(SysprofElf) debuginfo_elf = NULL;

      if (!(debuginfo_elf = sysprof_debuginfod_task_find_debuginfo (task, self->client, path, build_id, NULL)))
        return NULL;

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
sysprof_debuginfod_symbolizer_dispose (GObject *object)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  g_hash_table_remove_all (self->cache);

  G_OBJECT_CLASS (sysprof_debuginfod_symbolizer_parent_class)->dispose (object);
}

static void
sysprof_debuginfod_symbolizer_finalize (GObject *object)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  g_clear_object (&self->loader);

  g_clear_pointer (&self->cache, g_hash_table_unref);
  g_clear_pointer (&self->client, debuginfod_end);

  G_OBJECT_CLASS (sysprof_debuginfod_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_debuginfod_symbolizer_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  SysprofDebuginfodSymbolizer *self = SYSPROF_DEBUGINFOD_SYMBOLIZER (object);

  switch (prop_id)
    {
    case PROP_PROGRESS:
      g_value_set_double (value, self->progress);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_debuginfod_symbolizer_class_init (SysprofDebuginfodSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->dispose = sysprof_debuginfod_symbolizer_dispose;
  object_class->finalize = sysprof_debuginfod_symbolizer_finalize;
  object_class->get_property = sysprof_debuginfod_symbolizer_get_property;

  symbolizer_class->symbolize = sysprof_debuginfod_symbolizer_symbolize;

  properties[PROP_PROGRESS] =
    g_param_spec_double ("progress", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_debuginfod_symbolizer_init (SysprofDebuginfodSymbolizer *self)
{
}

SysprofSymbolizer *
sysprof_debuginfod_symbolizer_new (GError **error)
{
  g_autoptr(SysprofDebuginfodSymbolizer) self = NULL;

  self = g_object_new (SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, NULL);
  self->client = debuginfod_begin ();

  if (self->client == NULL)
    {
      int errsv = errno;
      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errsv),
                           g_strerror (errsv));
      return NULL;
    }

  debuginfod_set_user_data (self->client, self);
  debuginfod_set_progressfn (self->client, client_progress_callback);

  self->loader = sysprof_elf_loader_new ();
  self->cache = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);

  return SYSPROF_SYMBOLIZER (g_steal_pointer (&self));
}
