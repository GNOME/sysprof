/* sysprof-chart-layer.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_CHART_LAYER (sysprof_chart_layer_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofChartLayer, sysprof_chart_layer, SYSPROF, CHART_LAYER, GtkWidget)

struct _SysprofChartLayerClass
{
  GtkWidgetClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_chart_layer_get_title (SysprofChartLayer *self);
SYSPROF_AVAILABLE_IN_ALL
void        sysprof_chart_layer_set_title (SysprofChartLayer *self,
                                           const char        *title);

G_END_DECLS
