/* sysprof-process-model-item.h
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include "sysprof-version-macros.h"

#include <gio/gio.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROCESS_MODEL_ITEM (sysprof_process_model_item_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofProcessModelItem, sysprof_process_model_item, SYSPROF, PROCESS_MODEL_ITEM, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofProcessModelItem *sysprof_process_model_item_new_from_variant (GVariant                *info);
SYSPROF_AVAILABLE_IN_ALL
guint                    sysprof_process_model_item_hash             (SysprofProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                 sysprof_process_model_item_equal            (SysprofProcessModelItem *self,
                                                                      SysprofProcessModelItem *other);
SYSPROF_AVAILABLE_IN_ALL
GPid                     sysprof_process_model_item_get_pid          (SysprofProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar             *sysprof_process_model_item_get_command_line (SysprofProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                 sysprof_process_model_item_is_kernel        (SysprofProcessModelItem *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar * const     *sysprof_process_model_item_get_argv         (SysprofProcessModelItem *self);

G_END_DECLS
