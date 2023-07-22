/*
 * Copyright © 2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */


#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define EGG_TYPE_BITSET (egg_bitset_get_type ())

typedef struct _EggBitset EggBitset;

GType                   egg_bitset_get_type                     (void) G_GNUC_CONST;
EggBitset *             egg_bitset_ref                          (EggBitset              *self);
void                    egg_bitset_unref                        (EggBitset              *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EggBitset, egg_bitset_unref)

gboolean                egg_bitset_contains                     (const EggBitset        *self,
                                                                 guint                   value);
gboolean                egg_bitset_is_empty                     (const EggBitset        *self);
gboolean                egg_bitset_equals                       (const EggBitset        *self,
                                                                 const EggBitset        *other);
guint64                 egg_bitset_get_size                     (const EggBitset        *self);
guint64                 egg_bitset_get_size_in_range            (const EggBitset        *self,
                                                                 guint                   first,
                                                                 guint                   last);
guint                   egg_bitset_get_nth                      (const EggBitset        *self,
                                                                 guint                   nth);
guint                   egg_bitset_get_minimum                  (const EggBitset        *self);
guint                   egg_bitset_get_maximum                  (const EggBitset        *self);

EggBitset *             egg_bitset_new_empty                    (void);
EggBitset *             egg_bitset_copy                         (const EggBitset        *self);
EggBitset *             egg_bitset_new_range                    (guint                   start,
                                                                 guint                   n_items);

void                    egg_bitset_remove_all                   (EggBitset              *self);
gboolean                egg_bitset_add                          (EggBitset              *self,
                                                                 guint                   value);
gboolean                egg_bitset_remove                       (EggBitset              *self,
                                                                 guint                   value);
void                    egg_bitset_add_range                    (EggBitset              *self,
                                                                 guint                   start,
                                                                 guint                   n_items);
void                    egg_bitset_remove_range                 (EggBitset              *self,
                                                                 guint                   start,
                                                                 guint                   n_items);
void                    egg_bitset_add_range_closed             (EggBitset              *self,
                                                                 guint                   first,
                                                                 guint                   last);
void                    egg_bitset_remove_range_closed          (EggBitset              *self,
                                                                 guint                   first,
                                                                 guint                   last);
void                    egg_bitset_add_rectangle                (EggBitset              *self,
                                                                 guint                   start,
                                                                 guint                   width,
                                                                 guint                   height,
                                                                 guint                   stride);
void                    egg_bitset_remove_rectangle             (EggBitset              *self,
                                                                 guint                   start,
                                                                 guint                   width,
                                                                 guint                   height,
                                                                 guint                   stride);

void                    egg_bitset_union                        (EggBitset              *self,
                                                                 const EggBitset        *other);
void                    egg_bitset_intersect                    (EggBitset              *self,
                                                                 const EggBitset        *other);
void                    egg_bitset_subtract                     (EggBitset              *self,
                                                                 const EggBitset        *other);
void                    egg_bitset_difference                   (EggBitset              *self,
                                                                 const EggBitset        *other);
void                    egg_bitset_shift_left                   (EggBitset              *self,
                                                                 guint                   amount);
void                    egg_bitset_shift_right                  (EggBitset              *self,
                                                                 guint                   amount);
void                    egg_bitset_splice                       (EggBitset              *self,
                                                                 guint                   position,
                                                                 guint                   removed,
                                                                 guint                   added);

/**
 * EggBitsetIter:
 *
 * An opaque, stack-allocated struct for iterating
 * over the elements of a `EggBitset`.
 *
 * Before a `EggBitsetIter` can be used, it needs to be initialized with
 * [func@Egg.BitsetIter.init_first], [func@Egg.BitsetIter.init_last]
 * or [func@Egg.BitsetIter.init_at].
 */
typedef struct _EggBitsetIter EggBitsetIter;

struct _EggBitsetIter
{
  /*< private >*/
  gpointer private_data[10];
};

GType                   egg_bitset_iter_get_type                (void) G_GNUC_CONST;
gboolean                egg_bitset_iter_init_first              (EggBitsetIter          *iter,
                                                                 const EggBitset        *set,
                                                                 guint                  *value);
gboolean                egg_bitset_iter_init_last               (EggBitsetIter          *iter,
                                                                 const EggBitset        *set,
                                                                 guint                  *value);
gboolean                egg_bitset_iter_init_at                 (EggBitsetIter          *iter,
                                                                 const EggBitset        *set,
                                                                 guint                   target,
                                                                 guint                  *value);
gboolean                egg_bitset_iter_next                    (EggBitsetIter          *iter,
                                                                 guint                  *value);
gboolean                egg_bitset_iter_previous                (EggBitsetIter          *iter,
                                                                 guint                  *value);
guint                   egg_bitset_iter_get_value               (const EggBitsetIter    *iter);
gboolean                egg_bitset_iter_is_valid                (const EggBitsetIter    *iter);

G_END_DECLS

