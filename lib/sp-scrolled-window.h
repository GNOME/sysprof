/* sp-scrolled-window.h
 *
 * Copyright (C) 2014 Christian Hergert <christian@hergert.me>
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

#ifndef SP_SCROLLED_WINDOW_H
#define SP_SCROLLED_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SP_TYPE_SCROLLED_WINDOW (sp_scrolled_window_get_type())

G_DECLARE_DERIVABLE_TYPE (SpScrolledWindow, sp_scrolled_window, SP, SCROLLED_WINDOW, GtkScrolledWindow)

struct _SpScrolledWindowClass
{
  GtkScrolledWindowClass parent_class;
};

GtkWidget *sp_scrolled_window_new                    (void);
gint       sp_scrolled_window_get_max_content_height (SpScrolledWindow *self);
void       sp_scrolled_window_set_max_content_height (SpScrolledWindow *self,
                                                      gint              max_content_height);
gint       sp_scrolled_window_get_max_content_width  (SpScrolledWindow *self);
void       sp_scrolled_window_set_max_content_width  (SpScrolledWindow *self,
                                                      gint              max_content_width);

G_END_DECLS

#endif /* SP_SCROLLED_WINDOW_H */
