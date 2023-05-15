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

#include "sysprof-elf-symbolizer.h"
#include "sysprof-document-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofElfSymbolizer
{
  SysprofSymbolizer parent_instance;
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
  if (context != SYSPROF_ADDRESS_CONTEXT_NONE &&
      context != SYSPROF_ADDRESS_CONTEXT_USER)
    return NULL;

  return NULL;
}

static void
sysprof_elf_symbolizer_finalize (GObject *object)
{
  SysprofElfSymbolizer *self = (SysprofElfSymbolizer *)object;

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
}

SysprofSymbolizer *
sysprof_elf_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_SYMBOLIZER, NULL);
}
