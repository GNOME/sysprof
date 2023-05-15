/* sysprof-elf-symbolizer.c
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

#include "../libsysprof/binfile.h"

#include "sysprof-elf-symbolizer.h"
#include "sysprof-document-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofElfSymbolizer
{
  SysprofSymbolizer  parent_instance;
  GHashTable        *bin_file_cache;
};

struct _SysprofElfSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofElfSymbolizer, sysprof_elf_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static SysprofSymbol *
sysprof_elf_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                  SysprofStrings           *strings,
                                  const SysprofProcessInfo *process_info,
                                  SysprofAddressContext     context,
                                  SysprofAddress            address)
{
  SysprofElfSymbolizer *self = (SysprofElfSymbolizer *)symbolizer;
  SysprofDocumentMmap *map;
  g_autofree char *name = NULL;
  g_auto(GStrv) translations = NULL;
  GMappedFile *mapped_file = NULL;
  const char *path;

  if (process_info == NULL ||
      process_info->address_layout == NULL ||
      process_info->mount_namespace == NULL ||
      (context != SYSPROF_ADDRESS_CONTEXT_NONE &&
       context != SYSPROF_ADDRESS_CONTEXT_USER))
    return NULL;

  /* First find out what was mapped at that address */
  if (!(map = sysprof_address_layout_lookup (process_info->address_layout, address)))
    return NULL;

  /* The file could be available at a number of locations in case there
   * is an overlayfs, flatpak runtime, etc. Additionally, we may need
   * to resolve through various debug directories. All of those also
   * need to get translated to a location where this process can access
   * then (which itself might be via /var/run/host/... or similar).
   */
  path = sysprof_document_mmap_get_file (map);

  /* TODO:
   *
   * We need something like bin_file_t here that will let us look at
   * all of our possible translations for the file (overlayfs, etc)
   * and add debug directories on top of that. The debug directories
   * can be used to follow .gnu_debuglink through debug dirs.
   */

#if 0
  if (!(translations = sysprof_mount_namespace_translate (process_info->mount_namespace, path)))
    goto fallback;

  for (guint i = 0; translations[i]; i++)
    {
      /* If the file exists within our cache already, re-use that instead
       * of re-opening a binfile.
       */
      if ((mapped_file = g_hash_table_lookup (self->bin_file_cache, translations[i])))
        break;

      if ((mapped_file = g_mapped_file_new (translations[i], FALSE, NULL)))
        {
          g_hash_table_insert (self->bin_file_cache,
                               g_strdup (translations[i]),
                               mapped_file);
          break;
        }
    }

  if (mapped_file == NULL)
    goto fallback;
#endif

fallback:
  /* Fallback, we failed to locate the symbol within a file we can
   * access, so tell the user about what file contained the symbol
   * and the offset of the ELF section mapped.
   */
  name = g_strdup_printf ("In file %s <+0x%"G_GINT64_MODIFIER"x>",
                          sysprof_document_mmap_get_file (map),
                          sysprof_document_mmap_get_file_offset (map));

  return _sysprof_symbol_new (sysprof_strings_get (strings, name),
                              NULL,
                              NULL,
                              sysprof_document_mmap_get_start_address (map),
                              sysprof_document_mmap_get_end_address (map));
}

static void
sysprof_elf_symbolizer_finalize (GObject *object)
{
  SysprofElfSymbolizer *self = (SysprofElfSymbolizer *)object;

  g_clear_pointer (&self->bin_file_cache, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_elf_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_elf_symbolizer_class_init (SysprofElfSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_elf_symbolizer_finalize;

  symbolizer_class->symbolize = sysprof_elf_symbolizer_symbolize;
}

static void
sysprof_elf_symbolizer_init (SysprofElfSymbolizer *self)
{
  self->bin_file_cache = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                (GDestroyNotify)bin_file_free);
}

SysprofSymbolizer *
sysprof_elf_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_SYMBOLIZER, NULL);
}
