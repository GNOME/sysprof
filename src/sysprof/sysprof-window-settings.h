/* sysprof-window-settings.h
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

#ifndef SYSPROF_WINDOW_SETTINGS_H
#define SYSPROF_WINDOW_SETTINGS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void sysprof_window_settings_register (GtkWindow *window);

G_END_DECLS

#endif /* SYSPROF_WINDOW_SETTINGS_H */
