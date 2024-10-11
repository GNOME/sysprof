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

enum {
  PROP_0,
  PROP_DEBUG_DIRS,
  PROP_EXTERNAL_DEBUG_DIRS,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

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
  const char *nick = NULL;
  const char *path;
  const char *build_id;
  guint64 map_begin;
  guint64 map_end;
  guint64 relative_address;
  guint64 begin_address;
  guint64 end_address;
  guint64 file_inode;
  guint64 file_offset;
  SysprofSymbol *ret;

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

  map_begin = sysprof_document_mmap_get_start_address (map);
  map_end = sysprof_document_mmap_get_end_address (map);

  g_assert (address >= map_begin);
  g_assert (address < map_end);

  file_offset = sysprof_document_mmap_get_file_offset (map);

  relative_address = address;
  relative_address -= map_begin;
  relative_address += file_offset;

  path = sysprof_document_mmap_get_file (map);
  build_id = sysprof_document_mmap_get_build_id (map);
  file_inode = sysprof_document_mmap_get_file_inode (map);

  /* See if we can load an ELF at the path . It will be translated from the
   * mount namespace into something hopefully we can access.
   */
  if (!(elf = sysprof_elf_loader_load (self->loader,
                                       process_info->mount_namespace,
                                       path,
                                       build_id,
                                       file_inode,
                                       NULL)))
    return NULL;

  nick = sysprof_elf_get_nick (elf);

  /* Try to get the symbol name at the address and the begin/end address
   * so that it can be inserted into our symbol cache.
   */
  if (!(name = sysprof_elf_get_symbol_at_address (elf,
                                                  relative_address,
                                                  &begin_address,
                                                  &end_address)))
    return NULL;

  /* Sanitize address ranges if we have to. Sometimes that can happen
   * for us, but it seems to be limited to glibc.
   */
  begin_address = CLAMP (begin_address, file_offset, file_offset + (map_end - map_begin));
  end_address = CLAMP (end_address, file_offset, file_offset + (map_end - map_begin));
  if (end_address == begin_address)
    end_address++;

  ret = _sysprof_symbol_new (sysprof_strings_get (strings, name),
                             sysprof_strings_get (strings, path),
                             sysprof_strings_get (strings, nick),
                             map_begin + (begin_address - file_offset),
                             map_begin + (end_address - file_offset),
                             SYSPROF_SYMBOL_KIND_USER);

  return ret;
}

static void
sysprof_elf_symbolizer_loader_notify_cb (SysprofElfSymbolizer *self,
                                         GParamSpec           *pspec,
                                         SysprofElfLoader     *loader)
{
  g_assert (SYSPROF_IS_ELF_SYMBOLIZER (self));
  g_assert (pspec != NULL);
  g_assert (SYSPROF_IS_ELF_LOADER (loader));

  if (0) {}
  else if (g_strcmp0 (pspec->name, "debug-dirs") == 0)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEBUG_DIRS]);
  else if (g_strcmp0 (pspec->name, "external-debug-dirs") == 0)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXTERNAL_DEBUG_DIRS]);
}

static void
sysprof_elf_symbolizer_finalize (GObject *object)
{
  SysprofElfSymbolizer *self = (SysprofElfSymbolizer *)object;

  g_clear_object (&self->loader);

  G_OBJECT_CLASS (sysprof_elf_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_elf_symbolizer_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofElfSymbolizer *self = SYSPROF_ELF_SYMBOLIZER (object);

  switch (prop_id)
    {
    case PROP_DEBUG_DIRS:
      g_value_set_boxed (value, sysprof_elf_symbolizer_get_debug_dirs (self));
      break;

    case PROP_EXTERNAL_DEBUG_DIRS:
      g_value_set_boxed (value, sysprof_elf_symbolizer_get_external_debug_dirs (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_symbolizer_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofElfSymbolizer *self = SYSPROF_ELF_SYMBOLIZER (object);

  switch (prop_id)
    {
    case PROP_DEBUG_DIRS:
      sysprof_elf_symbolizer_set_debug_dirs (self, g_value_get_boxed (value));
      break;

    case PROP_EXTERNAL_DEBUG_DIRS:
      sysprof_elf_symbolizer_set_external_debug_dirs (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_symbolizer_class_init (SysprofElfSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_elf_symbolizer_finalize;
  object_class->get_property = sysprof_elf_symbolizer_get_property;
  object_class->set_property = sysprof_elf_symbolizer_set_property;

  symbolizer_class->symbolize = sysprof_elf_symbolizer_symbolize;

  properties[PROP_DEBUG_DIRS] =
    g_param_spec_boxed ("debug-dirs", NULL, NULL,
                         G_TYPE_STRV,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_EXTERNAL_DEBUG_DIRS] =
    g_param_spec_boxed ("external-debug-dirs", NULL, NULL,
                         G_TYPE_STRV,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_elf_symbolizer_init (SysprofElfSymbolizer *self)
{
  self->loader = sysprof_elf_loader_new ();

  g_signal_connect_object (self->loader,
                           "notify",
                           G_CALLBACK (sysprof_elf_symbolizer_loader_notify_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

SysprofSymbolizer *
sysprof_elf_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_SYMBOLIZER, NULL);
}

const char * const *
sysprof_elf_symbolizer_get_debug_dirs (SysprofElfSymbolizer *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF_SYMBOLIZER (self), NULL);

  return sysprof_elf_loader_get_debug_dirs (self->loader);
}

void
sysprof_elf_symbolizer_set_debug_dirs (SysprofElfSymbolizer *self,
                                       const char * const   *debug_dirs)
{
  g_return_if_fail (SYSPROF_IS_ELF_SYMBOLIZER (self));

  sysprof_elf_loader_set_debug_dirs (self->loader, debug_dirs);
}

const char * const *
sysprof_elf_symbolizer_get_external_debug_dirs (SysprofElfSymbolizer *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF_SYMBOLIZER (self), NULL);

  return sysprof_elf_loader_get_external_debug_dirs (self->loader);
}

void
sysprof_elf_symbolizer_set_external_debug_dirs (SysprofElfSymbolizer *self,
                                                const char * const   *external_debug_dirs)
{
  g_return_if_fail (SYSPROF_IS_ELF_SYMBOLIZER (self));

  sysprof_elf_loader_set_external_debug_dirs (self->loader, external_debug_dirs);
}
