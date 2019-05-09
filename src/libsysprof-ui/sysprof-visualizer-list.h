/* sysprof-visualizer-list.h
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "sysprof-capture-reader.h"
#include "sysprof-zoom-manager.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_VISUALIZER_LIST (sysprof_visualizer_list_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofVisualizerList, sysprof_visualizer_list, SYSPROF, VISUALIZER_LIST, GtkListBox)

struct _SysprofVisualizerListClass
{
  GtkListBoxClass parent_class;

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
  gpointer _reserved5;
  gpointer _reserved6;
  gpointer _reserved7;
  gpointer _reserved8;
};

GtkWidget       *sysprof_visualizer_list_new              (void);
void             sysprof_visualizer_list_set_reader       (SysprofVisualizerList *self,
                                                      SysprofCaptureReader  *reader);
SysprofCaptureReader *sysprof_visualizer_list_get_reader       (SysprofVisualizerList *self);
SysprofZoomManager   *sysprof_visualizer_list_get_zoom_manager (SysprofVisualizerList *self);
void             sysprof_visualizer_list_set_zoom_manager (SysprofVisualizerList *self,
                                                      SysprofZoomManager    *zoom_manager);

G_END_DECLS
