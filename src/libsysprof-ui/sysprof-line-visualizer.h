/* sysprof-line-visualizer.h
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

#include "sysprof-visualizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_LINE_VISUALIZER (sysprof_line_visualizer_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofLineVisualizer, sysprof_line_visualizer, SYSPROF, LINE_VISUALIZER, SysprofVisualizer)

struct _SysprofLineVisualizerClass
{
  SysprofVisualizerClass parent_class;

  void (*counter_added) (SysprofLineVisualizer *self,
                         guint                counter_id);

  /*< private >*/
  gpointer _reserved[16];
};

GtkWidget *sysprof_line_visualizer_new            (void);
void       sysprof_line_visualizer_clear          (SysprofLineVisualizer *self);
void       sysprof_line_visualizer_add_counter    (SysprofLineVisualizer *self,
                                                   guint                  counter_id,
                                                   const GdkRGBA         *color);
void       sysprof_line_visualizer_set_line_width (SysprofLineVisualizer *self,
                                                   guint                  counter_id,
                                                   gdouble                width);
void       sysprof_line_visualizer_set_fill       (SysprofLineVisualizer *self,
                                                   guint                  counter_id,
                                                   const GdkRGBA         *color);
void       sysprof_line_visualizer_set_dash       (SysprofLineVisualizer *self,
                                                   guint                  counter_id,
                                                   gboolean               use_dash);

G_END_DECLS
