/* sysprof-window.h
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

#ifndef SYSPROF_WINDOW_H
#define SYSPROF_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_WINDOW (sysprof_window_get_type())

G_DECLARE_FINAL_TYPE (SysprofWindow, sysprof_window, SYSPROF, WINDOW, GtkApplicationWindow)

typedef enum
{
  SYSPROF_WINDOW_STATE_0,
  SYSPROF_WINDOW_STATE_EMPTY,
  SYSPROF_WINDOW_STATE_FAILED,
  SYSPROF_WINDOW_STATE_RECORDING,
  SYSPROF_WINDOW_STATE_PROCESSING,
  SYSPROF_WINDOW_STATE_BROWSING,
} SysprofWindowState;

SysprofWindowState  sysprof_window_get_state          (SysprofWindow  *self);
gboolean       sysprof_window_get_recording      (SysprofWindow  *self);
GFile         *sysprof_window_get_capture_file   (SysprofWindow  *self);
void           sysprof_window_set_capture_file   (SysprofWindow  *self,
                                             GFile     *capture_file);
void           sysprof_window_open               (SysprofWindow  *self,
                                             GFile     *file);
void           sysprof_window_open_from_dialog   (SysprofWindow  *self);

G_END_DECLS

#endif /* SYSPROF_WINDOW_H */

