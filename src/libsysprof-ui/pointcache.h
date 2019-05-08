/* pointcache.h
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _PointCache PointCache;

typedef struct
{
  gdouble x;
  gdouble y;
} Point;

PointCache  *point_cache_new              (void);
PointCache  *point_cache_ref              (PointCache *self);
void         point_cache_unref            (PointCache *self);
void         point_cache_add_set          (PointCache *self,
                                           guint       set_id);
gboolean     point_cache_contains_set     (PointCache *self,
                                           guint       set_id);
void         point_cache_add_point_to_set (PointCache *self,
                                           guint       set_id,
                                           gdouble     x,
                                           gdouble     y);
const Point *point_cache_get_points       (PointCache *self,
                                           guint       set_id,
                                           guint      *n_points);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PointCache, point_cache_unref)

G_END_DECLS
