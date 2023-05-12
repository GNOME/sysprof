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

#include "sysprof-no-symbolizer.h"

struct _SysprofNoSymbolizer
{
  SysprofSymbolizer parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofNoSymbolizer, sysprof_no_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static SysprofSymbol *
sysprof_no_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                 SysprofStrings           *strings,
                                 const SysprofProcessInfo *process_info,
                                 SysprofAddress            address)
{
  return NULL;
}

static void
sysprof_no_symbolizer_class_init (SysprofNoSymbolizerClass *klass)
{
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  symbolizer_class->symbolize = sysprof_no_symbolizer_symbolize;
}

static void
sysprof_no_symbolizer_init (SysprofNoSymbolizer *self)
{
}

SysprofSymbolizer *
sysprof_no_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_NO_SYMBOLIZER, NULL);
}
