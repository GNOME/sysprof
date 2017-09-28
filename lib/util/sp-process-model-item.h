/* sp-process-model-item.h
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

#ifndef SP_PROCESS_MODEL_ITEM_H
#define SP_PROCESS_MODEL_ITEM_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SP_TYPE_PROCESS_MODEL_ITEM (sp_process_model_item_get_type())

G_DECLARE_FINAL_TYPE (SpProcessModelItem, sp_process_model_item, SP, PROCESS_MODEL_ITEM, GObject)

SpProcessModelItem  *sp_process_model_item_new              (GPid                pid);
guint                sp_process_model_item_hash             (SpProcessModelItem *self);
gboolean             sp_process_model_item_equal            (SpProcessModelItem *self,
                                                             SpProcessModelItem *other);
GPid                 sp_process_model_item_get_pid          (SpProcessModelItem *self);
const gchar         *sp_process_model_item_get_command_line (SpProcessModelItem *self);
gboolean             sp_process_model_item_is_kernel        (SpProcessModelItem *self);
const gchar * const *sp_process_model_item_get_argv         (SpProcessModelItem *self);

G_END_DECLS

#endif /* SP_PROCESS_MODEL_ITEM_H */
