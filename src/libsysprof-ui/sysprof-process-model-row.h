/* sysprof-process-model-row.h
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gtk/gtk.h>
#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROCESS_MODEL_ROW (sysprof_process_model_row_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofProcessModelRow, sysprof_process_model_row, SYSPROF, PROCESS_MODEL_ROW, GtkListBoxRow)

struct _SysprofProcessModelRowClass
{
  GtkListBoxRowClass parent;

  gpointer padding[4];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget               *sysprof_process_model_row_new          (SysprofProcessModelItem *item);
SYSPROF_AVAILABLE_IN_ALL
SysprofProcessModelItem *sysprof_process_model_row_get_item     (SysprofProcessModelRow  *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                 sysprof_process_model_row_get_selected (SysprofProcessModelRow  *self);
SYSPROF_AVAILABLE_IN_ALL
void                     sysprof_process_model_row_set_selected (SysprofProcessModelRow  *self,
                                                                 gboolean                 selected);

G_END_DECLS
