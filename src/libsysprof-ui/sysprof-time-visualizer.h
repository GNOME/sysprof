/* sysprof-time-visualizer.h
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

#define SYSPROF_TYPE_TIME_VISUALIZER (sysprof_time_visualizer_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofTimeVisualizer, sysprof_time_visualizer, SYSPROF, TIME_VISUALIZER, SysprofVisualizer)

struct _SysprofTimeVisualizerClass
{
  SysprofVisualizerClass parent_class;

  void (*counter_added) (SysprofTimeVisualizer *self,
                         guint                counter_id);

  /*< private >*/
  gpointer _reserved[16];
};

GtkWidget *sysprof_time_visualizer_new            (void);
void       sysprof_time_visualizer_clear          (SysprofTimeVisualizer *self);
void       sysprof_time_visualizer_add_counter    (SysprofTimeVisualizer *self,
                                                   guint                  counter_id,
                                                   const GdkRGBA         *color);
void       sysprof_time_visualizer_set_line_width (SysprofTimeVisualizer *self,
                                                   guint                  counter_id,
                                                   gdouble                width);
void       sysprof_time_visualizer_set_dash       (SysprofTimeVisualizer *self,
                                                   guint                  counter_id,
                                                   gboolean               use_dash);

G_END_DECLS
