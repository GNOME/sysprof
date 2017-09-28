/* sp-process-model.h
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

#ifndef SP_PROCESS_MODEL_H
#define SP_PROCESS_MODEL_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SP_TYPE_PROCESS_MODEL (sp_process_model_get_type())

G_DECLARE_FINAL_TYPE (SpProcessModel, sp_process_model, SP, PROCESS_MODEL, GObject)

SpProcessModel *sp_process_model_new          (void);
void            sp_process_model_queue_reload (SpProcessModel *self);

G_END_DECLS

#endif /* SP_PROCESS_MODEL_H */
