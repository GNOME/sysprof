/* sp-visualizer-ticks.h
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

#ifndef SP_VISUALIZER_TICKS_H
#define SP_VISUALIZER_TICKS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SP_TYPE_VISUALIZER_TICKS (sp_visualizer_ticks_get_type())

G_DECLARE_FINAL_TYPE (SpVisualizerTicks, sp_visualizer_ticks, SP, VISUALIZER_TICKS, GtkDrawingArea)

GtkWidget *sp_visualizer_ticks_new            (void);
void       sp_visualizer_ticks_set_epoch      (SpVisualizerTicks *self,
                                               gint64             epoch);
gint64     sp_visualizer_ticks_get_epoch      (SpVisualizerTicks *self);
void       sp_visualizer_ticks_get_time_range (SpVisualizerTicks *self,
                                               gint64            *begin_time,
                                               gint64            *end_time);
void       sp_visualizer_ticks_set_time_range (SpVisualizerTicks *self,
                                               gint64             begin_time,
                                               gint64             end_time);

G_END_DECLS

#endif /* SP_VISUALIZER_TICKS_H */
