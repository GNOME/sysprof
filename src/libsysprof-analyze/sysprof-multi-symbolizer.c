/* sysprof-multi-symbolizer.c
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

#include "sysprof-multi-symbolizer.h"
#include "sysprof-symbolizer-private.h"

struct _SysprofMultiSymbolizer
{
  SysprofSymbolizer parent_instance;
  GPtrArray *symbolizers;
};

struct _SysprofMultiSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofMultiSymbolizer, sysprof_multi_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static void
sysprof_multi_symbolizer_finalize (GObject *object)
{
  SysprofMultiSymbolizer *self = (SysprofMultiSymbolizer *)object;

  g_clear_pointer (&self->symbolizers, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_multi_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_multi_symbolizer_class_init (SysprofMultiSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_multi_symbolizer_finalize;
}

static void
sysprof_multi_symbolizer_init (SysprofMultiSymbolizer *self)
{
  self->symbolizers = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofMultiSymbolizer *
sysprof_multi_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_MULTI_SYMBOLIZER, NULL);
}

void
sysprof_multi_symbolizer_add (SysprofMultiSymbolizer *self,
                              SysprofSymbolizer      *symbolizer)
{
  g_return_if_fail (SYSPROF_IS_MULTI_SYMBOLIZER (self));
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer));

  g_ptr_array_add (self->symbolizers, g_object_ref (symbolizer));
}
