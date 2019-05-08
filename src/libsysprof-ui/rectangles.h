/* rectangles.h
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _Rectangles Rectangles;

Rectangles *rectangles_new           (gint64       begin_time,
                                      gint64       end_time);
void        rectangles_free          (Rectangles  *self);
void        rectangles_draw          (Rectangles  *self,
                                      GtkWidget   *widget,
                                      cairo_t     *cr);
void        rectangles_add           (Rectangles  *self,
                                      gint64       begin_time,
                                      gint64       end_time,
                                      const gchar *name,
                                      const gchar *message);
void        rectangles_set_end_time  (Rectangles  *self,
                                      gint64       end_time);
gboolean    rectangles_query_tooltip (Rectangles  *self,
                                      GtkTooltip  *tooltip,
                                      const gchar *group,
                                      gint         x,
                                      gint         y);

G_END_DECLS
