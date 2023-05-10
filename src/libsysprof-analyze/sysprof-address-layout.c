/* sysprof-address-layout.c
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

#include "sysprof-address-layout-private.h"

struct _SysprofAddressLayout
{
  GObject    parent_instance;
  GSequence *mmaps;
};

G_DEFINE_FINAL_TYPE (SysprofAddressLayout, sysprof_address_layout, G_TYPE_OBJECT)

static void
sysprof_address_layout_finalize (GObject *object)
{
  SysprofAddressLayout *self = (SysprofAddressLayout *)object;

  g_clear_pointer (&self->mmaps, g_sequence_free);

  G_OBJECT_CLASS (sysprof_address_layout_parent_class)->finalize (object);
}

static void
sysprof_address_layout_class_init (SysprofAddressLayoutClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_address_layout_finalize;
}

static void
sysprof_address_layout_init (SysprofAddressLayout *self)
{
  self->mmaps = g_sequence_new (g_object_unref);
}

SysprofAddressLayout *
sysprof_address_layout_new (void)
{
  return g_object_new (SYSPROF_TYPE_ADDRESS_LAYOUT, NULL);
}

static int
sysprof_address_layout_compare (gconstpointer a,
                                gconstpointer b,
                                gpointer      user_data)
{
  SysprofDocumentMmap *mmap_a = (SysprofDocumentMmap *)a;
  SysprofDocumentMmap *mmap_b = (SysprofDocumentMmap *)b;
  guint64 begin_a = sysprof_document_mmap_get_start_address (mmap_a);
  guint64 begin_b = sysprof_document_mmap_get_start_address (mmap_b);

  if (begin_a < begin_b)
    return -1;

  if (begin_a > begin_b)
    return 1;

  return 0;
}

void
sysprof_address_layout_take (SysprofAddressLayout *self,
                             SysprofDocumentMmap  *map)
{
  g_return_if_fail (SYSPROF_IS_ADDRESS_LAYOUT (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_MMAP (map));

  g_sequence_insert_sorted (self->mmaps,
                            map,
                            sysprof_address_layout_compare,
                            NULL);
}

static int
sysprof_address_layout_lookup_func (gconstpointer a,
                                    gconstpointer b,
                                    gpointer      user_data)
{
  SysprofDocumentMmap *mmap_a = (SysprofDocumentMmap *)a;
  const gint64 *addr = b;

  if (*addr < sysprof_document_mmap_get_start_address (mmap_a))
    return 1;

  if (*addr >= sysprof_document_mmap_get_end_address (mmap_a))
    return -1;

  return 0;
}

SysprofDocumentMmap *
sysprof_address_layout_lookup (SysprofAddressLayout *self,
                               SysprofAddress        address)
{
  GSequenceIter *iter;

  g_return_val_if_fail (SYSPROF_IS_ADDRESS_LAYOUT (self), NULL);

  iter = g_sequence_lookup (self->mmaps,
                            &address,
                            sysprof_address_layout_lookup_func,
                            NULL);

  return iter ? g_sequence_get (iter) : NULL;
}
