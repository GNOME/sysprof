/* sysprof-map-lookaside.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include <glib.h>

#include "sysprof-map-lookaside.h"

static gint
sysprof_map_compare (gconstpointer a,
                     gconstpointer b,
                     gpointer      user_data)
{
  const SysprofMap *map_a = a;
  const SysprofMap *map_b = b;

  return sysprof_capture_address_compare (map_a->start, map_b->start);
}

static gint
sysprof_map_compare_in_range (gconstpointer a,
                              gconstpointer b,
                              gpointer      user_data)
{
  const SysprofMap *map_a = a;
  const SysprofMap *map_b = b;

  /*
   * map_b is the needle for the search.
   * Only map_b->start is set.
   */

  if ((map_b->start >= map_a->start) && (map_b->start < map_a->end))
    return 0;

  return sysprof_capture_address_compare (map_a->start, map_b->start);
}

static void
sysprof_map_free (gpointer data)
{
  SysprofMap *map = data;

  g_slice_free (SysprofMap, map);
}

SysprofMapLookaside *
sysprof_map_lookaside_new (void)
{
  SysprofMapLookaside *ret;

  ret = g_slice_new0 (SysprofMapLookaside);
  ret->seq = g_sequence_new (sysprof_map_free);
  ret->chunk = g_string_chunk_new (4096);
  ret->overlays = NULL;

  return ret;
}

void
sysprof_map_lookaside_free (SysprofMapLookaside *self)
{
  g_sequence_free (self->seq);
  g_string_chunk_free (self->chunk);
  g_slice_free (SysprofMapLookaside, self);
}

void
sysprof_map_lookaside_insert (SysprofMapLookaside *self,
                              const SysprofMap    *map)
{
  SysprofMap *copy;

  g_assert (self != NULL);
  g_assert (map != NULL);

  copy = g_slice_new (SysprofMap);
  copy->start = map->start;
  copy->end = map->end;
  copy->offset = map->offset;
  copy->inode = map->inode;
  copy->filename = g_string_chunk_insert_const (self->chunk, map->filename);

  g_sequence_insert_sorted (self->seq, copy, sysprof_map_compare, NULL);
}


void
sysprof_map_lookaside_overlay (SysprofMapLookaside *self,
                               const gchar         *src,
                               const gchar         *dst)
{
  SysprofMapOverlay overlay;

  g_assert (self != NULL);
  g_assert (src != NULL);
  g_assert (dst != NULL);

  if (!src[0] || !dst[0])
    return;

  if (self->overlays == NULL)
    self->overlays = g_array_new (FALSE, FALSE, sizeof (SysprofMapOverlay));

  overlay.src = g_string_chunk_insert_const (self->chunk, src);
  overlay.dst = g_string_chunk_insert_const (self->chunk, dst);
  g_array_append_val (self->overlays, overlay);
}

const SysprofMap *
sysprof_map_lookaside_lookup (SysprofMapLookaside   *self,
                              SysprofCaptureAddress  address)
{
  SysprofMap map = { address };
  GSequenceIter *iter;

  g_assert (self != NULL);

  iter = g_sequence_lookup (self->seq, &map, sysprof_map_compare_in_range, NULL);

  if (iter != NULL)
    return g_sequence_get (iter);

  return NULL;
}
