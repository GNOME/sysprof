/* sysprof-memory-map-private.h
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

#include <glib-object.h>

#include "sysprof-mapped-file-private.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MEMORY_MAP (sysprof_memory_map_get_type())

G_DECLARE_FINAL_TYPE (SysprofMemoryMap, sysprof_memory_map, SYSPROF, MEMORY_MAP, GObject)

SysprofMemoryMap  *sysprof_memory_map_new             (GPtrArray         *mapped_files);
SysprofMappedFile *sysprof_memory_map_find_at_address (SysprofMemoryMap  *self,
                                                       guint64            address);

typedef GPtrArray* SysprofMemoryMapBuilder;

static inline void
sysprof_memory_map_builder_init (SysprofMemoryMapBuilder *builder)
{
  *builder = g_ptr_array_new_with_free_func (g_object_unref);
}

static inline void
sysprof_memory_map_builder_add (SysprofMemoryMapBuilder *builder,
                                SysprofMappedFile       *mapped_file)
{
  g_ptr_array_add (*builder, mapped_file);
}

static inline SysprofMemoryMap *
sysprof_memory_map_builder_end (SysprofMemoryMapBuilder *builder)
{
  return sysprof_memory_map_new (g_steal_pointer (builder));
}

G_END_DECLS
