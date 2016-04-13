/* sp-cell-renderer-percent.h
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

#ifndef SP_CELL_RENDERER_PERCENT_H
#define SP_CELL_RENDERER_PERCENT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SP_TYPE_CELL_RENDERER_PERCENT            (sp_cell_renderer_percent_get_type())
#define SP_CELL_RENDERER_PERCENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SP_TYPE_CELL_RENDERER_PERCENT, SpCellRendererPercent))
#define SP_CELL_RENDERER_PERCENT_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), SP_TYPE_CELL_RENDERER_PERCENT, SpCellRendererPercent const))
#define SP_CELL_RENDERER_PERCENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  SP_TYPE_CELL_RENDERER_PERCENT, SpCellRendererPercentClass))
#define SP_IS_CELL_RENDERER_PERCENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SP_TYPE_CELL_RENDERER_PERCENT))
#define SP_IS_CELL_RENDERER_PERCENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  SP_TYPE_CELL_RENDERER_PERCENT))
#define SP_CELL_RENDERER_PERCENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  SP_TYPE_CELL_RENDERER_PERCENT, SpCellRendererPercentClass))

typedef struct _SpCellRendererPercent      SpCellRendererPercent;
typedef struct _SpCellRendererPercentClass SpCellRendererPercentClass;

struct _SpCellRendererPercent
{
  GtkCellRendererText parent;
};

struct _SpCellRendererPercentClass
{
  GtkCellRendererTextClass parent_class;
};

GType            sp_cell_renderer_percent_get_type    (void);
GtkCellRenderer *sp_cell_renderer_percent_new         (void);
gdouble          sp_cell_renderer_percent_get_percent (SpCellRendererPercent *self);
void             sp_cell_renderer_percent_set_percent (SpCellRendererPercent *self,
                                                       gdouble                percent);

G_END_DECLS

#endif /* SP_CELL_RENDERER_PERCENT_H */
