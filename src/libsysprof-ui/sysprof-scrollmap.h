/* sysprof-scrollmap.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define SYSPROF_TYPE_SCROLLMAP (sysprof_scrollmap_get_type())

G_DECLARE_FINAL_TYPE (SysprofScrollmap, sysprof_scrollmap, SYSPROF, SCROLLMAP, GtkScrollbar)

void sysprof_scrollmap_set_timings    (SysprofScrollmap *self,
                                       GArray           *timings);
void sysprof_scrollmap_set_time_range (SysprofScrollmap *self,
                                       gint64            begin_time,
                                       gint64            end_time);

G_END_DECLS
