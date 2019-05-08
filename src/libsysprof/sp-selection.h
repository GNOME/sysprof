/* sp-selection.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include <glib-object.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SP_TYPE_SELECTION (sp_selection_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpSelection, sp_selection, SP, SELECTION, GObject)

typedef void (*SpSelectionForeachFunc) (SpSelection *self,
                                        gint64       begin_time,
                                        gint64       end_time,
                                        gpointer     user_data);

SYSPROF_AVAILABLE_IN_ALL
gboolean     sp_selection_get_has_selection (SpSelection            *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean     sp_selection_contains          (SpSelection            *self,
                                             gint64                  time_at);
SYSPROF_AVAILABLE_IN_ALL
void         sp_selection_select_range      (SpSelection            *self,
                                             gint64                  begin_time,
                                             gint64                  end_time);
SYSPROF_AVAILABLE_IN_ALL
void         sp_selection_unselect_range    (SpSelection            *self,
                                             gint64                  begin,
                                             gint64                  end);
SYSPROF_AVAILABLE_IN_ALL
void         sp_selection_unselect_all      (SpSelection            *self);
SYSPROF_AVAILABLE_IN_ALL
void         sp_selection_foreach           (SpSelection            *self,
                                             SpSelectionForeachFunc  foreach_func,
                                             gpointer                user_data);
SYSPROF_AVAILABLE_IN_ALL
SpSelection *sp_selection_copy              (const SpSelection      *self);

G_END_DECLS
