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

#include "timsort/gtktimsortprivate.h"

#include <gio/gio.h>

#include <eggbitset.h>

#include "sysprof-address-layout-private.h"

struct _SysprofAddressLayout
{
  GObject    parent_instance;
  GPtrArray *mmaps;
  guint      mmaps_dirty : 1;
};

static guint
sysprof_address_layout_get_n_items (GListModel *model)
{
  return SYSPROF_ADDRESS_LAYOUT (model)->mmaps->len;
}

static GType
sysprof_address_layout_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_DOCUMENT_MMAP;
}

static gpointer
sysprof_address_layout_get_item (GListModel *model,
                                 guint       position)
{
  SysprofAddressLayout *self = SYSPROF_ADDRESS_LAYOUT (model);

  if (position >= self->mmaps->len)
    return NULL;

  return g_object_ref (g_ptr_array_index (self->mmaps, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = sysprof_address_layout_get_n_items;
  iface->get_item = sysprof_address_layout_get_item;
  iface->get_item_type = sysprof_address_layout_get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofAddressLayout, sysprof_address_layout, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_address_layout_finalize (GObject *object)
{
  SysprofAddressLayout *self = (SysprofAddressLayout *)object;

  g_clear_pointer (&self->mmaps, g_ptr_array_unref);

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
  self->mmaps = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofAddressLayout *
sysprof_address_layout_new (void)
{
  return g_object_new (SYSPROF_TYPE_ADDRESS_LAYOUT, NULL);
}

void
sysprof_address_layout_take (SysprofAddressLayout *self,
                             SysprofDocumentMmap  *map)
{
  g_return_if_fail (SYSPROF_IS_ADDRESS_LAYOUT (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_MMAP (map));

  g_ptr_array_add (self->mmaps, map);

  self->mmaps_dirty = TRUE;
}

static int
compare_mmaps (gconstpointer a,
               gconstpointer b)
{
  SysprofDocumentMmap *mmap_a = *(SysprofDocumentMmap * const *)a;
  SysprofDocumentMmap *mmap_b = *(SysprofDocumentMmap * const *)b;
  guint64 begin_a = sysprof_document_mmap_get_start_address (mmap_a);
  guint64 begin_b = sysprof_document_mmap_get_start_address (mmap_b);
  guint64 end_a = sysprof_document_mmap_get_end_address (mmap_a);
  guint64 end_b = sysprof_document_mmap_get_end_address (mmap_b);

  if (begin_a < begin_b)
    return -1;

  if (begin_a > begin_b)
    return 1;

  if (end_a < end_b)
    return -1;

  if (end_a > end_b)
    return 1;

  return 0;
}

static gboolean
mmaps_overlap (SysprofDocumentMmap *a,
               SysprofDocumentMmap *b)
{
  if ((sysprof_document_mmap_get_start_address (a) <= sysprof_document_mmap_get_start_address (b)) &&
      (sysprof_document_mmap_get_end_address (a) > sysprof_document_mmap_get_start_address (b)))
    return TRUE;

  return FALSE;
}

static EggBitset *
find_duplicates (GPtrArray *sorted)
{
  EggBitset *bitset = egg_bitset_new_empty ();

  if (sorted->len == 0)
    return bitset;

  for (guint i = 0; i < sorted->len-1; i++)
    {
      SysprofDocumentMmap *map = g_ptr_array_index (sorted, i);
      SysprofDocumentMmap *next = g_ptr_array_index (sorted, i+1);

      /* Take the second one if they overlap, which is generally the large of
       * the mmaps. That can happen when something like [stack] is resized into
       * a larger mmap. It's useful to remove duplicates so we get more
       * predictable bsearch results.
       */
      if (mmaps_overlap (map, next))
        egg_bitset_add (bitset, i);
    }

  return bitset;
}

static int
find_by_address (gconstpointer a,
                 gconstpointer b)
{
  const SysprofAddress *key = a;
  SysprofDocumentMmap *map = *(SysprofDocumentMmap * const *)b;

  if (*key < sysprof_document_mmap_get_start_address (map))
    return -1;

  if (*key >= sysprof_document_mmap_get_end_address (map))
    return 1;

  return 0;
}

SysprofDocumentMmap *
sysprof_address_layout_lookup (SysprofAddressLayout *self,
                               SysprofAddress        address)
{
  SysprofDocumentMmap **ret;

  g_return_val_if_fail (SYSPROF_IS_ADDRESS_LAYOUT (self), NULL);

  if (self->mmaps_dirty)
    {
      g_autoptr(EggBitset) dups = NULL;
      EggBitsetIter iter;
      guint old_len = self->mmaps->len;
      guint i;

      self->mmaps_dirty = FALSE;

      gtk_tim_sort (self->mmaps->pdata,
                    self->mmaps->len,
                    sizeof (gpointer),
                    (GCompareDataFunc)compare_mmaps,
                    NULL);
      dups = find_duplicates (self->mmaps);

      if (egg_bitset_iter_init_last (&iter, dups, &i))
        {
          do
            g_ptr_array_remove_index (self->mmaps, i);
          while (egg_bitset_iter_previous (&iter, &i));
        }

      g_list_model_items_changed (G_LIST_MODEL (self), 0, old_len, self->mmaps->len);
    }

  if (self->mmaps->len > 0)
    ret = bsearch (&address,
                   self->mmaps->pdata,
                   self->mmaps->len,
                   sizeof (gpointer),
                   find_by_address);
  else
    ret = NULL;

  return ret ? *ret : NULL;
}
