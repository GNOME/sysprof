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

#include "sysprof-elf-private.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-elf-symbolizer.h"
#include "sysprof-document-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofElfSymbolizer
{
  SysprofSymbolizer  parent_instance;
  SysprofElfLoader  *loader;
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
  g_autoptr(SysprofElf) elf = NULL;
  SysprofDocumentMmap *map;
  g_autofree char *name = NULL;
  const char *path;
  const char *build_id;
  guint64 start_address;
  guint64 relative_address;
  guint64 begin_address;
  guint64 end_address;
  guint64 file_inode;
  guint64 file_offset;

  if (process_info == NULL ||
      process_info->address_layout == NULL ||
      process_info->mount_namespace == NULL ||
      (context != SYSPROF_ADDRESS_CONTEXT_NONE &&
       context != SYSPROF_ADDRESS_CONTEXT_USER))
    return NULL;

  /* Always ignore jitmap functions, no matter the ordering */
  if ((address & 0xFFFFFFFF00000000) == 0xE000000000000000)
    return NULL;

  /* First find out what was mapped at that address */
  if (!(map = sysprof_address_layout_lookup (process_info->address_layout, address)))
    return NULL;

  path = sysprof_document_mmap_get_file (map);
  build_id = sysprof_document_mmap_get_build_id (map);
  file_inode = sysprof_document_mmap_get_file_inode (map);
  file_offset = sysprof_document_mmap_get_file_offset (map);
  start_address = sysprof_document_mmap_get_start_address (map);
  relative_address = file_offset + (address - start_address);

  /* See if we can load an ELF at the path . It will be translated from the
   * mount namespace into something hopefully we can access.
   */
  if (!(elf = sysprof_elf_loader_load (self->loader,
                                       process_info->mount_namespace,
                                       path,
                                       build_id,
                                       file_inode,
                                       NULL)))
    goto fallback;

  /* Try to get the symbol name at the address and the begin/end address
   * so that it can be inserted into our symbol cache.
   */
  if (!(name = sysprof_elf_get_symbol_at_address (elf,
                                                  relative_address,
                                                  &begin_address,
                                                  &end_address)))
    goto fallback;

  return _sysprof_symbol_new (sysprof_strings_get (strings, name),
                              sysprof_strings_get (strings, sysprof_elf_get_nick (elf)),
                              sysprof_strings_get (strings, path),
                              start_address + (begin_address - file_offset),
                              start_address + (end_address - file_offset));

fallback:
  /* Fallback, we failed to locate the symbol within a file we can
   * access, so tell the user about what file contained the symbol
   * and where (relative to that file) the IP was.
   */
  name = g_strdup_printf ("In File %s+0x%"G_GINT64_MODIFIER"x",
                          sysprof_document_mmap_get_file (map),
                          relative_address);
  begin_address = address;
  end_address = address + 1;
  return _sysprof_symbol_new (sysprof_strings_get (strings, name),
                              NULL, NULL, begin_address, end_address);
}

static void
sysprof_elf_symbolizer_finalize (GObject *object)
{
  SysprofElfSymbolizer *self = (SysprofElfSymbolizer *)object;

  g_clear_object (&self->loader);

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
  self->loader = sysprof_elf_loader_new ();
}

SysprofSymbolizer *
sysprof_elf_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_SYMBOLIZER, NULL);
}
