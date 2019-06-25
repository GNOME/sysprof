/* sysprof-model-filter.h
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

#include <gio/gio.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MODEL_FILTER (sysprof_model_filter_get_type())

typedef gboolean (*SysprofModelFilterFunc) (GObject  *object,
                                            gpointer  user_data);

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofModelFilter, sysprof_model_filter, SYSPROF, MODEL_FILTER, GObject)

struct _SysprofModelFilterClass
{
  GObjectClass parent_class;

  gpointer padding[8];
};

SYSPROF_AVAILABLE_IN_ALL
SysprofModelFilter *sysprof_model_filter_new             (GListModel             *child_model);
SYSPROF_AVAILABLE_IN_ALL
GListModel         *sysprof_model_filter_get_child_model (SysprofModelFilter     *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_model_filter_invalidate      (SysprofModelFilter     *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_model_filter_set_filter_func (SysprofModelFilter     *self,
                                                          SysprofModelFilterFunc  filter_func,
                                                          gpointer                filter_func_data,
                                                          GDestroyNotify          filter_func_data_destroy);

G_END_DECLS
