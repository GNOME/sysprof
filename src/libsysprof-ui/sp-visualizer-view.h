/* sp-visualizer-view.h
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

#include <gtk/gtk.h>
#include <sysprof.h>

#include "sp-visualizer-row.h"
#include "sp-selection.h"
#include "sp-zoom-manager.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SP_TYPE_VISUALIZER_VIEW (sp_visualizer_view_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SpVisualizerView, sp_visualizer_view, SP, VISUALIZER_VIEW, GtkBin)

struct _SpVisualizerViewClass
{
  GtkBinClass parent_class;

  void (*visualizer_added)   (SpVisualizerView *self,
                              SpVisualizerRow  *visualizer);
  void (*visualizer_removed) (SpVisualizerView *self,
                              SpVisualizerRow  *visualizer);

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
GtkWidget       *sp_visualizer_view_new              (void);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureReader *sp_visualizer_view_get_reader       (SpVisualizerView *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_visualizer_view_set_reader       (SpVisualizerView *self,
                                                      SpCaptureReader  *reader);
SYSPROF_AVAILABLE_IN_ALL
SpZoomManager   *sp_visualizer_view_get_zoom_manager (SpVisualizerView *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_visualizer_view_set_zoom_manager (SpVisualizerView *self,
                                                      SpZoomManager    *zoom_manager);
SYSPROF_AVAILABLE_IN_ALL
SpSelection     *sp_visualizer_view_get_selection    (SpVisualizerView *self);

G_END_DECLS
