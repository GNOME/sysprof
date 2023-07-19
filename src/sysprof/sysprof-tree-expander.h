/* sysprof-tree-expander.h
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_TREE_EXPANDER (sysprof_tree_expander_get_type())

G_DECLARE_FINAL_TYPE (SysprofTreeExpander, sysprof_tree_expander, SYSPROF, TREE_EXPANDER, GtkWidget)

GtkWidget      *sysprof_tree_expander_new                    (void);
GMenuModel     *sysprof_tree_expander_get_menu_model         (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_menu_model         (SysprofTreeExpander *self,
                                                              GMenuModel          *menu_model);
GIcon          *sysprof_tree_expander_get_icon               (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_icon               (SysprofTreeExpander *self,
                                                              GIcon               *icon);
void            sysprof_tree_expander_set_icon_name          (SysprofTreeExpander *self,
                                                              const char          *icon_name);
GIcon          *sysprof_tree_expander_get_expanded_icon      (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_expanded_icon      (SysprofTreeExpander *self,
                                                              GIcon               *icon);
void            sysprof_tree_expander_set_expanded_icon_name (SysprofTreeExpander *self,
                                                              const char          *expanded_icon_name);
GtkWidget      *sysprof_tree_expander_get_child              (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_child              (SysprofTreeExpander *self,
                                                              GtkWidget           *child);
GtkWidget      *sysprof_tree_expander_get_suffix             (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_suffix             (SysprofTreeExpander *self,
                                                              GtkWidget           *suffix);
GtkTreeListRow *sysprof_tree_expander_get_list_row           (SysprofTreeExpander *self);
void            sysprof_tree_expander_set_list_row           (SysprofTreeExpander *self,
                                                              GtkTreeListRow      *list_row);
gpointer        sysprof_tree_expander_get_item               (SysprofTreeExpander *self);
void            sysprof_tree_expander_show_popover           (SysprofTreeExpander *self,
                                                              GtkPopover          *popover);

G_END_DECLS
