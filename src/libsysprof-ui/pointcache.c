/* pointcache.c
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

#define G_LOG_DOMAIN "pointcache"

#include "pointcache.h"

struct _PointCache
{
  volatile gint  ref_count;
  GHashTable    *sets;
};

static void
point_cache_finalize (PointCache *self)
{
  g_clear_pointer (&self->sets, g_hash_table_unref);
  g_slice_free (PointCache, self);
}

PointCache *
point_cache_ref (PointCache *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
point_cache_unref (PointCache *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    point_cache_finalize (self);
}

PointCache *
point_cache_new (void)
{
  PointCache *self;

  self = g_slice_new0 (PointCache);
  self->ref_count = 1;
  self->sets = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)g_array_unref);

  return self;
}

void
point_cache_add_set (PointCache *self,
                     guint       set_id)
{
  g_hash_table_insert (self->sets,
                       GUINT_TO_POINTER (set_id),
                       g_array_new (FALSE, FALSE, sizeof (Point)));
}

gboolean
point_cache_contains_set (PointCache *self,
                          guint       set_id)
{
  return g_hash_table_contains (self->sets, GUINT_TO_POINTER (set_id));
}

void
point_cache_add_point_to_set (PointCache *self,
                              guint       set_id,
                              gdouble     x,
                              gdouble     y)
{
  GArray *ar;
  Point point = { x, y };

  ar = g_hash_table_lookup (self->sets, GUINT_TO_POINTER (set_id));
  g_array_append_val (ar, point);
}

const Point *
point_cache_get_points (PointCache *self,
                        guint       set_id,
                        guint      *n_points)
{
  GArray *ar;

  *n_points = 0;

  if ((ar = g_hash_table_lookup (self->sets, GUINT_TO_POINTER (set_id))))
    {
      *n_points = ar->len;
      return &g_array_index (ar, const Point, 0);
    }

  return NULL;
}
