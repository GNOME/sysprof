/* sysprof-memory-map.c
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

#include <stdlib.h>

#include "sysprof-memory-map-private.h"

struct _SysprofMemoryMap
{
  GObject    parent_instance;
  GPtrArray *mapped_files;
};

G_DEFINE_FINAL_TYPE (SysprofMemoryMap, sysprof_memory_map, G_TYPE_OBJECT)

static void
sysprof_memory_map_finalize (GObject *object)
{
  SysprofMemoryMap *self = (SysprofMemoryMap *)object;

  g_clear_pointer (&self->mapped_files, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_memory_map_parent_class)->finalize (object);
}

static void
sysprof_memory_map_class_init (SysprofMemoryMapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_memory_map_finalize;
}

static void
sysprof_memory_map_init (SysprofMemoryMap *self)
{
  self->mapped_files = g_ptr_array_new_with_free_func (g_object_unref);
}

static int
mapped_file_compare (gconstpointer aptr,
                     gconstpointer bptr)
{
  SysprofMappedFile * const *a = aptr;
  SysprofMappedFile * const *b = bptr;
  guint64 aaddr = sysprof_mapped_file_get_address (*a);
  guint64 baddr = sysprof_mapped_file_get_address (*b);

  if (aaddr < baddr)
    return -1;
  else if (aaddr > baddr)
    return 1;
  else
    return 0;
}

static int
mapped_file_bsearch (gconstpointer keyptr,
                     gconstpointer elemptr)
{
  guint64 address = *(const guint64 *)keyptr;
  SysprofMappedFile *mapped_file = *(SysprofMappedFile * const *)elemptr;
  guint64 begin = sysprof_mapped_file_get_address (mapped_file);
  guint64 length = sysprof_mapped_file_get_length (mapped_file);

  if (address < begin)
    return -1;

  if (address >= begin + length)
    return 1;

  return 0;
}

SysprofMemoryMap *
sysprof_memory_map_new (GPtrArray *mapped_files)
{
  SysprofMemoryMap *self;

  g_return_val_if_fail (mapped_files != NULL, NULL);

  g_ptr_array_set_free_func (mapped_files, g_object_unref);
  g_ptr_array_sort (mapped_files, mapped_file_compare);

  self = g_object_new (SYSPROF_TYPE_MEMORY_MAP, NULL);
  self->mapped_files = mapped_files;

  return self;
}

SysprofMappedFile *
sysprof_memory_map_find_at_address (SysprofMemoryMap *self,
                                    guint64           address)
{
  SysprofMappedFile **match;

  g_return_val_if_fail (SYSPROF_IS_MEMORY_MAP (self), NULL);

  match = bsearch (&address,
                   (gpointer)self->mapped_files->pdata,
                   self->mapped_files->len,
                   sizeof (gpointer),
                   mapped_file_bsearch);

  return match ? *match : NULL;
}
