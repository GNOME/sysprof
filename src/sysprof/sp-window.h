/* sp-window.h
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_WINDOW_H
#define SP_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SP_TYPE_WINDOW (sp_window_get_type())

G_DECLARE_FINAL_TYPE (SpWindow, sp_window, SP, WINDOW, GtkApplicationWindow)

typedef enum
{
  SP_WINDOW_STATE_0,
  SP_WINDOW_STATE_EMPTY,
  SP_WINDOW_STATE_FAILED,
  SP_WINDOW_STATE_RECORDING,
  SP_WINDOW_STATE_PROCESSING,
  SP_WINDOW_STATE_BROWSING,
} SpWindowState;

SpWindowState  sp_window_get_state          (SpWindow  *self);
gboolean       sp_window_get_recording      (SpWindow  *self);
GFile         *sp_window_get_capture_file   (SpWindow  *self);
void           sp_window_set_capture_file   (SpWindow  *self,
                                             GFile     *capture_file);
void           sp_window_open               (SpWindow  *self,
                                             GFile     *file);
void           sp_window_open_from_dialog   (SpWindow  *self);

G_END_DECLS

#endif /* SP_WINDOW_H */

