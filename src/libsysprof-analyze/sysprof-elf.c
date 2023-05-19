/* sysprof-elf.c
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

#include "../libsysprof/elfparser.h"

#include "sysprof-elf-private.h"

struct _SysprofElf
{
  GObject parent_instance;
  char *build_id;
  char *debug_link;
  char *file;
  SysprofElf *debug_link_elf;
  ElfParser *parser;
};

enum {
  PROP_0,
  PROP_BUILD_ID,
  PROP_DEBUG_LINK,
  PROP_DEBUG_LINK_ELF,
  PROP_FILE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofElf, sysprof_elf, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_elf_finalize (GObject *object)
{
  SysprofElf *self = (SysprofElf *)object;

  g_clear_pointer (&self->build_id, g_free);
  g_clear_pointer (&self->debug_link, g_free);
  g_clear_pointer (&self->file, g_free);
  g_clear_pointer (&self->parser, elf_parser_free);
  g_clear_object (&self->debug_link_elf);

  G_OBJECT_CLASS (sysprof_elf_parent_class)->finalize (object);
}

static void
sysprof_elf_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SysprofElf *self = SYSPROF_ELF (object);

  switch (prop_id)
    {
    case PROP_BUILD_ID:
      g_value_set_string (value, sysprof_elf_get_build_id (self));
      break;

    case PROP_DEBUG_LINK:
      g_value_set_string (value, sysprof_elf_get_debug_link (self));
      break;

    case PROP_DEBUG_LINK_ELF:
      g_value_set_object (value, sysprof_elf_get_debug_link_elf (self));
      break;

    case PROP_FILE:
      g_value_set_string (value, sysprof_elf_get_file (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SysprofElf *self = SYSPROF_ELF (object);

  switch (prop_id)
    {
    case PROP_DEBUG_LINK_ELF:
      sysprof_elf_set_debug_link_elf (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_class_init (SysprofElfClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_elf_finalize;
  object_class->get_property = sysprof_elf_get_property;
  object_class->set_property = sysprof_elf_set_property;

  properties [PROP_BUILD_ID] =
    g_param_spec_string ("build-id", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DEBUG_LINK] =
    g_param_spec_string ("debug-link", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DEBUG_LINK_ELF] =
    g_param_spec_object ("debug-link-elf", NULL, NULL,
                         SYSPROF_TYPE_ELF,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_string ("file", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_elf_init (SysprofElf *self)
{
}

SysprofElf *
sysprof_elf_new (const char   *filename,
                 GMappedFile  *mapped_file,
                 GError      **error)
{
  SysprofElf *self;
  ElfParser *parser;

  g_return_val_if_fail (mapped_file != NULL, NULL);

  if (!(parser = elf_parser_new_from_mmap (g_steal_pointer (&mapped_file), error)))
    return NULL;

  self = g_object_new (SYSPROF_TYPE_ELF, NULL);
  self->file = g_strdup (filename);
  self->parser = g_steal_pointer (&parser);

  return self;
}

const char *
sysprof_elf_get_file (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->file;
}

const char *
sysprof_elf_get_build_id (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->build_id;
}

const char *
sysprof_elf_get_debug_link (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->debug_link;
}

const char *
sysprof_elf_get_symbol_at_address (SysprofElf *self,
                                   guint64     address,
                                   guint64    *begin_address,
                                   guint64    *end_address)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);
  g_return_val_if_fail (begin_address != NULL, NULL);
  g_return_val_if_fail (end_address != NULL, NULL);

  *begin_address = *end_address = 0;

  return NULL;
}

/**
 * sysprof_elf_get_debug_link_elf:
 * @self: a #SysprofElf
 *
 * Gets a #SysprofElf that was resolved from the `.gnu_debuglink`
 * ELF section header.
 *
 * Returns: (transfer none) (nullable): a #SysprofElf or %NULL
 */
SysprofElf *
sysprof_elf_get_debug_link_elf (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->debug_link_elf;
}

void
sysprof_elf_set_debug_link_elf (SysprofElf *self,
                                SysprofElf *debug_link_elf)
{
  g_return_if_fail (SYSPROF_IS_ELF (self));
  g_return_if_fail (!debug_link_elf || SYSPROF_IS_ELF (debug_link_elf));

  if (g_set_object (&self->debug_link_elf, debug_link_elf))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DEBUG_LINK_ELF]);
}
