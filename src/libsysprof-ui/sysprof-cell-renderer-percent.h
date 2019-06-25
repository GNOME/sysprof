/* sysprof-cell-renderer-percent.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#define SYSPROF_TYPE_CELL_RENDERER_PERCENT            (sysprof_cell_renderer_percent_get_type())
#define SYSPROF_CELL_RENDERER_PERCENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYSPROF_TYPE_CELL_RENDERER_PERCENT, SysprofCellRendererPercent))
#define SYSPROF_CELL_RENDERER_PERCENT_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYSPROF_TYPE_CELL_RENDERER_PERCENT, SysprofCellRendererPercent const))
#define SYSPROF_CELL_RENDERER_PERCENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  SYSPROF_TYPE_CELL_RENDERER_PERCENT, SysprofCellRendererPercentClass))
#define SYSPROF_IS_CELL_RENDERER_PERCENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYSPROF_TYPE_CELL_RENDERER_PERCENT))
#define SYSPROF_IS_CELL_RENDERER_PERCENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  SYSPROF_TYPE_CELL_RENDERER_PERCENT))
#define SYSPROF_CELL_RENDERER_PERCENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  SYSPROF_TYPE_CELL_RENDERER_PERCENT, SysprofCellRendererPercentClass))

typedef struct _SysprofCellRendererPercent      SysprofCellRendererPercent;
typedef struct _SysprofCellRendererPercentClass SysprofCellRendererPercentClass;

struct _SysprofCellRendererPercent
{
  GtkCellRendererProgress parent;
};

struct _SysprofCellRendererPercentClass
{
  GtkCellRendererProgressClass parent_class;

  /*< private >*/
  gpointer _reserved[4];
};

GType            sysprof_cell_renderer_percent_get_type    (void);
GtkCellRenderer *sysprof_cell_renderer_percent_new         (void);
gdouble          sysprof_cell_renderer_percent_get_percent (SysprofCellRendererPercent *self);
void             sysprof_cell_renderer_percent_set_percent (SysprofCellRendererPercent *self,
                                                            gdouble                     percent);

G_END_DECLS
