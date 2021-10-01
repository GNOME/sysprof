/* egg-resizer.h
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EGG_TYPE_RESIZER (egg_resizer_get_type())

G_DECLARE_FINAL_TYPE (EggResizer, egg_resizer, EGG, RESIZER, GtkWidget)

GtkWidget       *egg_resizer_new          (GtkPositionType  position);
GtkPositionType  egg_resizer_get_position (EggResizer      *self);
void             egg_resizer_set_position (EggResizer      *self,
                                           GtkPositionType  position);
GtkWidget       *egg_resizer_get_child    (EggResizer      *self);
void             egg_resizer_set_child    (EggResizer      *self,
                                           GtkWidget       *child);
GtkWidget       *egg_resizer_get_handle   (EggResizer      *self);

G_END_DECLS
