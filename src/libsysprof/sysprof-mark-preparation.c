/* sysprof-mark-preparation.c
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-mark-preparation-private.h"
#include "sysprof-document-bitset-index-private.h"
#include "sysprof-mark-catalog-private.h"

static void
prepared_mark_free (gpointer data)
{
  SysprofPreparedMark *mark = data;

  g_free (mark->group);
  g_free (mark->name);
  g_clear_pointer (&mark->bits, egg_bitset_unref);
  g_clear_pointer (&mark->durations, g_array_unref);
  g_free (mark);
}

static int
prepared_mark_compare (gconstpointer a,
                       gconstpointer b)
{
  const SysprofPreparedMark *ma = *(SysprofPreparedMark * const *)a;
  const SysprofPreparedMark *mb = *(SysprofPreparedMark * const *)b;
  int ret = strcmp (ma->group, mb->group);

  return ret ? ret : strcmp (ma->name, mb->name);
}

static DexFuture *
prepare_fiber (gpointer data)
{
  SysprofDocumentStorage *storage = data;
  g_autoptr(SysprofDocumentReader) reader = _sysprof_document_reader_new (storage, NULL);
  g_autoptr(EggBitset) bits = _sysprof_document_storage_copy_marks (storage, NULL);
  g_autoptr(GPtrArray) prepared = g_ptr_array_new_with_free_func (prepared_mark_free);
  g_autoptr(GHashTable) groups = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        (GDestroyNotify)g_hash_table_unref);
  g_autoptr(GError) error = NULL;
  EggBitsetIter iter;
  guint position;
  guint count = 0;

  for (gboolean valid = egg_bitset_iter_init_first (&iter, bits, &position);
       valid;
       valid = egg_bitset_iter_next (&iter, &position))
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (G_LIST_MODEL (reader), position);
      const char *group = sysprof_document_mark_get_group (mark);
      const char *name = sysprof_document_mark_get_name (mark);
      GHashTable *names = g_hash_table_lookup (groups, group);
      SysprofPreparedMark *entry;
      gint64 duration = sysprof_document_mark_get_duration (mark);

      if (names == NULL)
        {
          names = g_hash_table_new (g_str_hash, g_str_equal);
          g_hash_table_insert (groups, g_strdup (group), names);
        }

      if (!(entry = g_hash_table_lookup (names, name)))
        {
          entry = g_new0 (SysprofPreparedMark, 1);
          entry->group = g_strdup (group);
          entry->name = g_strdup (name);
          entry->bits = egg_bitset_new_empty ();
          entry->durations = g_array_new (FALSE, FALSE, sizeof (gint64));
          entry->min = G_MAXINT64;
          entry->max = G_MININT64;
          g_ptr_array_add (prepared, entry);
          g_hash_table_insert (names, entry->name, entry);
        }

      egg_bitset_add (entry->bits, position);
      entry->min = MIN (entry->min, duration);
      entry->max = MAX (entry->max, duration);
      entry->total += duration;
      g_array_append_val (entry->durations, duration);

      if (++count % 4096 == 0 && !dex_fiber_yield (&error))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

  for (guint i = 0; i < prepared->len; i++)
    {
      SysprofPreparedMark *entry = g_ptr_array_index (prepared, i);

      entry->median = g_array_index (entry->durations, gint64, entry->durations->len / 2);
      g_clear_pointer (&entry->durations, g_array_unref);
    }

  g_ptr_array_sort (prepared, prepared_mark_compare);

  return dex_future_new_take_boxed (G_TYPE_PTR_ARRAY, g_steal_pointer (&prepared));
}

DexFuture *
_sysprof_mark_prepare (SysprofDocumentStorage *storage)
{
  g_return_val_if_fail (storage != NULL, NULL);

  return dex_scheduler_spawn (dex_thread_pool_scheduler_get_default (), 0,
                              prepare_fiber, _sysprof_document_storage_ref (storage),
                              (GDestroyNotify)_sysprof_document_storage_unref);
}

GListModel *
_sysprof_mark_prepare_publish (GPtrArray       *prepared,
                               SysprofDocument *document)
{
  g_autoptr(GListStore) groups = g_list_store_new (G_TYPE_LIST_MODEL);
  g_autoptr(GListStore) group = NULL;
  const char *last_group = NULL;

  g_return_val_if_fail (prepared != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  for (guint i = 0; i < prepared->len; i++)
    {
      SysprofPreparedMark *entry = g_ptr_array_index (prepared, i);
      g_autoptr(GListModel) model = NULL;
      g_autoptr(SysprofMarkCatalog) catalog = NULL;

      if (g_strcmp0 (last_group, entry->group) != 0)
        {
          g_clear_object (&group);
          group = g_list_store_new (SYSPROF_TYPE_MARK_CATALOG);
          g_list_store_append (groups, group);
          last_group = entry->group;
        }

      model = _sysprof_document_bitset_index_new_full (G_LIST_MODEL (document),
                                                       entry->bits,
                                                       SYSPROF_TYPE_DOCUMENT_MARK);
      catalog = _sysprof_mark_catalog_new (entry->group,
                                           entry->name,
                                           model,
                                           entry->min,
                                           entry->max,
                                           entry->total / (gint64)egg_bitset_get_size (entry->bits),
                                           entry->median);
      g_list_store_append (group, catalog);
    }

  return G_LIST_MODEL (g_steal_pointer (&groups));
}

typedef struct
{
  GPtrArray *prepared;
  GArray    *selected;
} Union;

static void
union_free (gpointer data)
{
  Union *state = data;

  g_ptr_array_unref (state->prepared);
  g_array_unref (state->selected);
  g_free (state);
}

static DexFuture *
union_fiber (gpointer data)
{
  Union *state = data;
  g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func ((GDestroyNotify)egg_bitset_unref);
  g_autoptr(GError) error = NULL;
  EggBitset *bits = egg_bitset_new_empty ();

  g_ptr_array_add (result, bits);

  for (guint i = 0; i < state->selected->len; i++)
    {
      guint index = g_array_index (state->selected, guint, i);
      SysprofPreparedMark *entry = g_ptr_array_index (state->prepared, index);

      egg_bitset_union (bits, entry->bits);

      if (!dex_fiber_yield (&error))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

  return dex_future_new_take_boxed (G_TYPE_PTR_ARRAY, g_steal_pointer (&result));
}

DexFuture *
_sysprof_mark_prepare_union (GPtrArray *prepared,
                             GArray    *selected)
{
  Union *state;

  g_return_val_if_fail (prepared != NULL, NULL);
  g_return_val_if_fail (selected != NULL, NULL);

  state = g_new0 (Union, 1);
  state->prepared = g_ptr_array_ref (prepared);
  state->selected = g_array_ref (selected);

  return dex_scheduler_spawn (dex_thread_pool_scheduler_get_default (), 0,
                              union_fiber, state, union_free);
}
