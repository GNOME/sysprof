/* sysprof-chart.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <sysprof-capture.h>

#include "sysprof-chart-layer.h"
#include "sysprof-session.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_CHART (sysprof_chart_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofChart, sysprof_chart, SYSPROF, CHART, GtkWidget)

struct _SysprofChartClass
{
  GtkWidgetClass parent_class;

  gboolean (*activate_layer_item) (SysprofChart      *self,
                                   SysprofChartLayer *layer,
                                   gpointer           item);
};

GtkWidget      *sysprof_chart_new          (void);
SysprofSession *sysprof_chart_get_session  (SysprofChart      *self);
void            sysprof_chart_set_session  (SysprofChart      *self,
                                            SysprofSession    *session);
const char     *sysprof_chart_get_title    (SysprofChart      *self);
void            sysprof_chart_set_title    (SysprofChart      *self,
                                            const char        *title);
void            sysprof_chart_add_layer    (SysprofChart      *self,
                                            SysprofChartLayer *layer);
void            sysprof_chart_remove_layer (SysprofChart      *self,
                                            SysprofChartLayer *layer);
GListModel      *sysprof_chart_get_model   (SysprofChart      *self);
void             sysprof_chart_set_model   (SysprofChart      *self,
                                            GListModel        *model);

G_END_DECLS
