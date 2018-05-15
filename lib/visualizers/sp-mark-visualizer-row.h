/* sp-mark-visualizer-row.h
 *
 * Copyright 2018 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include "visualizers/sp-visualizer-row.h"

G_BEGIN_DECLS

#define SP_TYPE_MARK_VISUALIZER_ROW (sp_mark_visualizer_row_get_type())

G_DECLARE_DERIVABLE_TYPE (SpMarkVisualizerRow, sp_mark_visualizer_row, SP, MARK_VISUALIZER_ROW, SpVisualizerRow)

struct _SpMarkVisualizerRowClass
{
  SpVisualizerRowClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

GtkWidget *sp_mark_visualizer_row_new      (void);
void       sp_mark_visualizer_row_add_mark (SpMarkVisualizerRow *self,
                                            GPid                 pid,
                                            GPid                 tid,
                                            const gchar         *name,
                                            const GdkRGBA       *color);

G_END_DECLS
