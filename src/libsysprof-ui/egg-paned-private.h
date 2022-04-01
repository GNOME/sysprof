/* egg-paned.h
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

#define EGG_TYPE_PANED (egg_paned_get_type())

G_DECLARE_FINAL_TYPE (EggPaned, egg_paned, EGG, PANED, GtkWidget)

GtkWidget *egg_paned_new            (void);
void       egg_paned_append         (EggPaned  *self,
                                     GtkWidget *child);
void       egg_paned_prepend        (EggPaned  *self,
                                     GtkWidget *child);
void       egg_paned_insert         (EggPaned  *self,
                                     int        position,
                                     GtkWidget *child);
void       egg_paned_insert_after   (EggPaned  *self,
                                     GtkWidget *child,
                                     GtkWidget *sibling);
void       egg_paned_remove         (EggPaned  *self,
                                     GtkWidget *child);
guint      egg_paned_get_n_children (EggPaned  *self);
GtkWidget *egg_paned_get_nth_child  (EggPaned  *self,
                                     guint      nth);

G_END_DECLS
