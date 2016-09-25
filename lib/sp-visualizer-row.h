/* sp-visualizer-row.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef SP_VISUALIZER_ROW_H
#define SP_VISUALIZER_ROW_H

#include <gtk/gtk.h>

#include "sp-capture-reader.h"

G_BEGIN_DECLS

#define SP_TYPE_VISUALIZER_ROW (sp_visualizer_row_get_type())

G_DECLARE_DERIVABLE_TYPE (SpVisualizerRow, sp_visualizer_row, SP, VISUALIZER_ROW, GtkListBoxRow)

struct _SpVisualizerRowClass
{
  GtkListBoxRowClass parent_class;

  /**
   * SpVisualizerRow::set_reader:
   *
   * Sets the reader that the row should use to extract counters.
   * This reader is private to the row and should be freed when
   * no longer in use with sp_capture_reader_unref().
   */
  void (*set_reader) (SpVisualizerRow *self,
                      SpCaptureReader *reader);

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

void sp_visualizer_row_set_reader (SpVisualizerRow *self,
                                   SpCaptureReader *reader);

G_END_DECLS

#endif /* SP_VISUALIZER_ROW_H */
