/* sp-cell-renderer-percent.h
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

#include "sysprof-version-macros.h"

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

  gpointer padding[4];
};

SYSPROF_AVAILABLE_IN_ALL
GType            sp_cell_renderer_percent_get_type    (void);
SYSPROF_AVAILABLE_IN_ALL
GtkCellRenderer *sp_cell_renderer_percent_new         (void);
SYSPROF_AVAILABLE_IN_ALL
gdouble          sp_cell_renderer_percent_get_percent (SpCellRendererPercent *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_cell_renderer_percent_set_percent (SpCellRendererPercent *self,
                                                       gdouble                percent);

G_END_DECLS
