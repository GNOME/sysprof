/* sysprof-visualizer-view.h
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
#include <sysprof.h>

#include "sysprof-visualizer-row.h"
#include "sysprof-selection.h"
#include "sysprof-zoom-manager.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_VISUALIZER_VIEW (sysprof_visualizer_view_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofVisualizerView, sysprof_visualizer_view, SYSPROF, VISUALIZER_VIEW, GtkBin)

struct _SysprofVisualizerViewClass
{
  GtkBinClass parent_class;

  void (*visualizer_added)   (SysprofVisualizerView *self,
                              SysprofVisualizerRow  *visualizer);
  void (*visualizer_removed) (SysprofVisualizerView *self,
                              SysprofVisualizerRow  *visualizer);

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
  gpointer _reserved5;
  gpointer _reserved6;
  gpointer _reserved7;
  gpointer _reserved8;
  gpointer _reserved9;
  gpointer _reserved10;
  gpointer _reserved11;
  gpointer _reserved12;
  gpointer _reserved13;
  gpointer _reserved14;
  gpointer _reserved15;
  gpointer _reserved16;
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget       *sysprof_visualizer_view_new              (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader *sysprof_visualizer_view_get_reader       (SysprofVisualizerView *self);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_visualizer_view_set_reader       (SysprofVisualizerView *self,
                                                      SysprofCaptureReader  *reader);
SYSPROF_AVAILABLE_IN_ALL
SysprofZoomManager   *sysprof_visualizer_view_get_zoom_manager (SysprofVisualizerView *self);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_visualizer_view_set_zoom_manager (SysprofVisualizerView *self,
                                                      SysprofZoomManager    *zoom_manager);
SYSPROF_AVAILABLE_IN_ALL
SysprofSelection     *sysprof_visualizer_view_get_selection    (SysprofVisualizerView *self);

G_END_DECLS
