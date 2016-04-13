/* sp-map-lookaside.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include "sp-map-lookaside.h"

struct _SpMapLookaside
{
  GSequence    *seq;
  GStringChunk *chunk;
};

static gint
sp_map_compare (gconstpointer a,
                gconstpointer b,
                gpointer      user_data)
{
  const SpMap *map_a = a;
  const SpMap *map_b = b;

  return sp_capture_address_compare (map_a->start, map_b->start);
}

static gint
sp_map_compare_in_range (gconstpointer a,
                         gconstpointer b,
                         gpointer      user_data)
{
  const SpMap *map_a = a;
  const SpMap *map_b = b;

  /*
   * map_b is the needle for the search.
   * Only map_b->start is set.
   */

  if ((map_b->start >= map_a->start) && (map_b->start < map_a->end))
    return 0;

  return sp_capture_address_compare (map_a->start, map_b->start);
}

static void
sp_map_free (gpointer data)
{
  SpMap *map = data;

  g_slice_free (SpMap, map);
}

SpMapLookaside *
sp_map_lookaside_new (void)
{
  SpMapLookaside *ret;

  ret = g_slice_new (SpMapLookaside);
  ret->seq = g_sequence_new (sp_map_free);
  ret->chunk = g_string_chunk_new (4096);

  return ret;
}

void
sp_map_lookaside_free (SpMapLookaside *self)
{
  g_sequence_free (self->seq);
  g_string_chunk_free (self->chunk);
  g_slice_free (SpMapLookaside, self);
}

void
sp_map_lookaside_insert (SpMapLookaside *self,
                         const SpMap    *map)
{
  SpMap *copy;

  g_assert (self != NULL);
  g_assert (map != NULL);

  copy = g_slice_new (SpMap);
  copy->start = map->start;
  copy->end = map->end;
  copy->offset = map->offset;
  copy->inode = map->inode;
  copy->filename = g_string_chunk_insert_const (self->chunk, map->filename);

  g_sequence_insert_sorted (self->seq, copy, sp_map_compare, NULL);
}

const SpMap *
sp_map_lookaside_lookup (SpMapLookaside   *self,
                         SpCaptureAddress  address)
{
  SpMap map = { address };
  GSequenceIter *iter;

  g_assert (self != NULL);

  iter = g_sequence_lookup (self->seq, &map, sp_map_compare_in_range, NULL);

  if (iter != NULL)
    return g_sequence_get (iter);

  return NULL;
}
