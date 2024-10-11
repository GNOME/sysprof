/* sysprof-debuginfod-symbolizer.h
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#define SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER         (sysprof_debuginfod_symbolizer_get_type())
#define SYSPROF_IS_DEBUGINFOD_SYMBOLIZER(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER)
#define SYSPROF_DEBUGINFOD_SYMBOLIZER(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, SysprofDebuginfodSymbolizer)
#define SYSPROF_DEBUGINFOD_SYMBOLIZER_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DEBUGINFOD_SYMBOLIZER, SysprofDebuginfodSymbolizerClass)

typedef struct _SysprofDebuginfodSymbolizer      SysprofDebuginfodSymbolizer;
typedef struct _SysprofDebuginfodSymbolizerClass SysprofDebuginfodSymbolizerClass;

SYSPROF_AVAILABLE_IN_48
GType              sysprof_debuginfod_symbolizer_get_type (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_48
SysprofSymbolizer *sysprof_debuginfod_symbolizer_new      (GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDebuginfodSymbolizer, g_object_unref)

G_END_DECLS
