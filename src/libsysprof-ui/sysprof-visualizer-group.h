/* sysprof-visualizer-group.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "sysprof-visualizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_VISUALIZER_GROUP (sysprof_visualizer_group_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofVisualizerGroup, sysprof_visualizer_group, SYSPROF, VISUALIZER_GROUP, GtkListBoxRow)

struct _SysprofVisualizerGroupClass
{
  GtkListBoxRowClass parent_class;

  void (*group_activated) (SysprofVisualizerGroup *self);

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
SysprofVisualizerGroup *sysprof_visualizer_group_new          (void);
SYSPROF_AVAILABLE_IN_ALL
GMenuModel             *sysprof_visualizer_group_get_menu     (SysprofVisualizerGroup *self);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_visualizer_group_set_menu     (SysprofVisualizerGroup *self,
                                                               GMenuModel             *menu);
SYSPROF_AVAILABLE_IN_ALL
gint                    sysprof_visualizer_group_get_priority (SysprofVisualizerGroup *self);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_visualizer_group_set_priority (SysprofVisualizerGroup *self,
                                                               gint                    priority);
SYSPROF_AVAILABLE_IN_ALL
const gchar            *sysprof_visualizer_group_get_title    (SysprofVisualizerGroup *self);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_visualizer_group_set_title    (SysprofVisualizerGroup *self,
                                                               const gchar            *title);
SYSPROF_AVAILABLE_IN_ALL
gboolean                sysprof_visualizer_group_get_has_page (SysprofVisualizerGroup *self);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_visualizer_group_set_has_page (SysprofVisualizerGroup *self,
                                                               gboolean                has_page);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_visualizer_group_insert       (SysprofVisualizerGroup *self,
                                                               SysprofVisualizer      *visualizer,
                                                               gint                    position,
                                                               gboolean                can_toggle);

G_END_DECLS
