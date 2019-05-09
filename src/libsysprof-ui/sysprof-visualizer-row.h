/* sysprof-visualizer-row.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#define SYSPROF_TYPE_VISUALIZER_ROW (sysprof_visualizer_row_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofVisualizerRow, sysprof_visualizer_row, SYSPROF, VISUALIZER_ROW, GtkListBoxRow)

typedef struct
{
  gdouble x;
  gdouble y;
} SysprofVisualizerRowRelativePoint;

typedef struct
{
  gint x;
  gint y;
} SysprofVisualizerRowAbsolutePoint;

struct _SysprofVisualizerRowClass
{
  GtkListBoxRowClass parent_class;

  /**
   * SysprofVisualizerRow::set_reader:
   *
   * Sets the reader that the row should use to extract counters.
   * This reader is private to the row and should be freed when
   * no longer in use with sysprof_capture_reader_unref().
   */
  void (*set_reader) (SysprofVisualizerRow *self,
                      SysprofCaptureReader *reader);

  /*< private >*/
  gpointer _reserved[16];
};

void           sysprof_visualizer_row_set_reader       (SysprofVisualizerRow *self,
                                                   SysprofCaptureReader *reader);
SysprofZoomManager *sysprof_visualizer_row_get_zoom_manager (SysprofVisualizerRow *self);
void           sysprof_visualizer_row_set_zoom_manager (SysprofVisualizerRow *self,
                                                   SysprofZoomManager   *zoom_manager);
void           sysprof_visualizer_row_translate_points (SysprofVisualizerRow                    *self,
                                                   const SysprofVisualizerRowRelativePoint *in_points,
                                                   guint                               n_in_points,
                                                   SysprofVisualizerRowAbsolutePoint       *out_points,
                                                   guint                               n_out_points);

G_END_DECLS
