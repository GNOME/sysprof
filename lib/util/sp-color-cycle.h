/* sp-color-cycle.h
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

#ifndef SP_COLOR_CYCLE_H
#define SP_COLOR_CYCLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SP_TYPE_COLOR_CYCLE (sp_color_cycle_get_type())

typedef struct _SpColorCycle SpColorCycle;

GType         sp_color_cycle_get_type (void);
SpColorCycle *sp_color_cycle_ref      (SpColorCycle *self);
void          sp_color_cycle_unref    (SpColorCycle *self);
SpColorCycle *sp_color_cycle_new      (void);
void          sp_color_cycle_reset    (SpColorCycle *self);
void          sp_color_cycle_next     (SpColorCycle *self,
                                       GdkRGBA      *rgba);

G_END_DECLS

#endif /* SP_COLOR_CYCLE_H */
