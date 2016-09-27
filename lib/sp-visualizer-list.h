/* sp-visualizer-list.h
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

#ifndef SP_VISUALIZER_LIST_H
#define SP_VISUALIZER_LIST_H

#include <gtk/gtk.h>

#include "sp-capture-reader.h"

G_BEGIN_DECLS

#define SP_TYPE_VISUALIZER_LIST (sp_visualizer_list_get_type())

G_DECLARE_DERIVABLE_TYPE (SpVisualizerList, sp_visualizer_list, SP, VISUALIZER_LIST, GtkListBox)

struct _SpVisualizerListClass
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

GtkWidget       *sp_visualizer_list_new            (void);
void             sp_visualizer_list_set_reader     (SpVisualizerList *self,
                                                    SpCaptureReader  *reader);
SpCaptureReader *sp_visualizer_list_get_reader     (SpVisualizerList *self);
void             sp_visualizer_list_set_time_range (SpVisualizerList *self,
                                                    gint64            begin_time,
                                                    gint64            end_time);
void             sp_visualizer_list_get_time_range (SpVisualizerList *self,
                                                    gint64           *begin_time,
                                                    gint64           *end_time);

G_END_DECLS

#endif /* SP_VISUALIZER_LIST_H */
