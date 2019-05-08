/* sp-process-model-row.h
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

#ifndef SP_PROCESS_MODEL_ROW_H
#define SP_PROCESS_MODEL_ROW_H

#include <gtk/gtk.h>

#include "sp-process-model-item.h"

G_BEGIN_DECLS

#define SP_TYPE_PROCESS_MODEL_ROW (sp_process_model_row_get_type())

G_DECLARE_DERIVABLE_TYPE (SpProcessModelRow, sp_process_model_row, SP, PROCESS_MODEL_ROW, GtkListBoxRow)

struct _SpProcessModelRowClass
{
  GtkListBoxRowClass parent;

  gpointer padding[4];
};

GtkWidget          *sp_process_model_row_new          (SpProcessModelItem *item);
SpProcessModelItem *sp_process_model_row_get_item     (SpProcessModelRow  *self);
gboolean            sp_process_model_row_get_selected (SpProcessModelRow  *self);
void                sp_process_model_row_set_selected (SpProcessModelRow  *self,
                                                       gboolean            selected);

G_END_DECLS

#endif /* SP_PROCESS_MODEL_ROW_H */

