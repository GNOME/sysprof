/* sysprof-line-visualizer-row.h
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

#include "sysprof-visualizer-row.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_LINE_VISUALIZER_ROW (sysprof_line_visualizer_row_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofLineVisualizerRow, sysprof_line_visualizer_row, SYSPROF, LINE_VISUALIZER_ROW, SysprofVisualizerRow)

struct _SysprofLineVisualizerRowClass
{
  SysprofVisualizerRowClass parent_class;

  void (*counter_added) (SysprofLineVisualizerRow *self,
                         guint                counter_id);

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget *sysprof_line_visualizer_row_new            (void);
SYSPROF_AVAILABLE_IN_ALL
void       sysprof_line_visualizer_row_clear          (SysprofLineVisualizerRow *self);
SYSPROF_AVAILABLE_IN_ALL
void       sysprof_line_visualizer_row_add_counter    (SysprofLineVisualizerRow *self,
                                                  guint                counter_id,
                                                  const GdkRGBA       *color);
SYSPROF_AVAILABLE_IN_ALL
void       sysprof_line_visualizer_row_set_line_width (SysprofLineVisualizerRow *self,
                                                  guint                counter_id,
                                                  gdouble              width);
SYSPROF_AVAILABLE_IN_ALL
void       sysprof_line_visualizer_row_set_fill       (SysprofLineVisualizerRow *self,
                                                  guint                counter_id,
                                                  const GdkRGBA       *color);

G_END_DECLS
