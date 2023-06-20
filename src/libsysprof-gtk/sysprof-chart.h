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

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget      *sysprof_chart_new          (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofSession *sysprof_chart_get_session  (SysprofChart      *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_chart_set_session  (SysprofChart      *self,
                                            SysprofSession    *session);
SYSPROF_AVAILABLE_IN_ALL
const char     *sysprof_chart_get_title    (SysprofChart      *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_chart_set_title    (SysprofChart      *self,
                                            const char        *title);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_chart_add_layer    (SysprofChart      *self,
                                            SysprofChartLayer *layer);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_chart_remove_layer (SysprofChart      *self,
                                            SysprofChartLayer *layer);

G_END_DECLS
