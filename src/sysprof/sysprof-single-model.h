/* sysprof-single-model.h
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

#include <gio/gio.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_SINGLE_MODEL (sysprof_single_model_get_type())

G_DECLARE_FINAL_TYPE (SysprofSingleModel, sysprof_single_model, SYSPROF, SINGLE_MODEL, GObject)

SysprofSingleModel *sysprof_single_model_new      (void);
gpointer            sysprof_single_model_get_item (SysprofSingleModel *self);
void                sysprof_single_model_set_item (SysprofSingleModel *self,
                                                   gpointer            item);

G_END_DECLS
