/* sp-process-model-item.h
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

#include "sysprof-version-macros.h"

#include <gio/gio.h>

G_BEGIN_DECLS

#define SP_TYPE_PROCESS_MODEL_ITEM (sp_process_model_item_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpProcessModelItem, sp_process_model_item, SP, PROCESS_MODEL_ITEM, GObject)

SYSPROF_AVAILABLE_IN_ALL
SpProcessModelItem  *sp_process_model_item_new              (GPid                pid);
SYSPROF_AVAILABLE_IN_ALL
guint                sp_process_model_item_hash             (SpProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean             sp_process_model_item_equal            (SpProcessModelItem *self,
                                                             SpProcessModelItem *other);
SYSPROF_AVAILABLE_IN_ALL
GPid                 sp_process_model_item_get_pid          (SpProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar         *sp_process_model_item_get_command_line (SpProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean             sp_process_model_item_is_kernel        (SpProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar * const *sp_process_model_item_get_argv         (SpProcessModelItem *self);

G_END_DECLS
