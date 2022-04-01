/* egg-three-grid.h
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EGG_TYPE_THREE_GRID        (egg_three_grid_get_type())
#define EGG_TYPE_THREE_GRID_COLUMN (egg_three_grid_column_get_type())

G_DECLARE_DERIVABLE_TYPE (EggThreeGrid, egg_three_grid, EGG, THREE_GRID, GtkWidget)

struct _EggThreeGridClass
{
  GtkWidgetClass parent_class;
};

typedef enum
{
  EGG_THREE_GRID_COLUMN_LEFT,
  EGG_THREE_GRID_COLUMN_CENTER,
  EGG_THREE_GRID_COLUMN_RIGHT
} EggThreeGridColumn;

GType      egg_three_grid_column_get_type (void);
GtkWidget *egg_three_grid_new             (void);
void       egg_three_grid_add             (EggThreeGrid       *self,
                                           GtkWidget          *child,
                                           guint               row,
                                           EggThreeGridColumn  column);
void       egg_three_grid_remove          (EggThreeGrid       *self,
                                           GtkWidget          *child);

G_END_DECLS
