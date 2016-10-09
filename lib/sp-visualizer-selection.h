/* sp-visualizer-selection.h
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SP_VISUALIZER_SELECTION_H
#define SP_VISUALIZER_SELECTION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SP_TYPE_VISUALIZER_SELECTION (sp_visualizer_selection_get_type())

G_DECLARE_FINAL_TYPE (SpVisualizerSelection, sp_visualizer_selection, SP, VISUALIZER_SELECTION, GObject)

typedef void (*SpVisualizerSelectionForeachFunc) (SpVisualizerSelection *self,
                                                  gint64                 begin_time,
                                                  gint64                 end_time,
                                                  gpointer               user_data);

gboolean sp_visualizer_selection_get_has_selection (SpVisualizerSelection            *self);
gboolean sp_visualizer_selection_contains          (SpVisualizerSelection            *self,
                                                    gint64                            time_at);
void     sp_visualizer_selection_select_range      (SpVisualizerSelection            *self,
                                                    gint64                            begin_time,
                                                    gint64                            end_time);
void     sp_visualizer_selection_unselect_range    (SpVisualizerSelection            *self,
                                                    gint64                            begin,
                                                    gint64                            end);
void     sp_visualizer_selection_unselect_all      (SpVisualizerSelection            *self);
void     sp_visualizer_selection_foreach           (SpVisualizerSelection            *self,
                                                    SpVisualizerSelectionForeachFunc  foreach_func,
                                                    gpointer                          user_data);
SpVisualizerSelection *
         sp_visualizer_selection_copy              (const SpVisualizerSelection      *self);

G_END_DECLS

#endif /* SP_VISUALIZER_SELECTION_H */
