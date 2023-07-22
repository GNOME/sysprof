/* sysprof-multi-symbolizer.h
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

#pragma once

#include "sysprof-symbolizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MULTI_SYMBOLIZER         (sysprof_multi_symbolizer_get_type())
#define SYSPROF_IS_MULTI_SYMBOLIZER(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_MULTI_SYMBOLIZER)
#define SYSPROF_MULTI_SYMBOLIZER(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_MULTI_SYMBOLIZER, SysprofMultiSymbolizer)
#define SYSPROF_MULTI_SYMBOLIZER_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_MULTI_SYMBOLIZER, SysprofMultiSymbolizerClass)

typedef struct _SysprofMultiSymbolizer      SysprofMultiSymbolizer;
typedef struct _SysprofMultiSymbolizerClass SysprofMultiSymbolizerClass;

SYSPROF_AVAILABLE_IN_ALL
GType                   sysprof_multi_symbolizer_get_type (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofMultiSymbolizer *sysprof_multi_symbolizer_new      (void);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_multi_symbolizer_take     (SysprofMultiSymbolizer *self,
                                                           SysprofSymbolizer      *symbolizer);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofMultiSymbolizer, g_object_unref)

G_END_DECLS
