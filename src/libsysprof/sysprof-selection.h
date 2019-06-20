/* sysprof-selection.h
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include <glib-object.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SELECTION (sysprof_selection_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSelection, sysprof_selection, SYSPROF, SELECTION, GObject)

typedef void (*SysprofSelectionForeachFunc) (SysprofSelection *self,
                                             gint64            begin_time,
                                             gint64            end_time,
                                             gpointer          user_data);

SYSPROF_AVAILABLE_IN_ALL
gboolean          sysprof_selection_get_has_selection (SysprofSelection            *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean          sysprof_selection_contains          (SysprofSelection            *self,
                                                       gint64                       time_at);
SYSPROF_AVAILABLE_IN_ALL
void              sysprof_selection_select_range      (SysprofSelection            *self,
                                                       gint64                       begin_time,
                                                       gint64                       end_time);
SYSPROF_AVAILABLE_IN_ALL
void              sysprof_selection_unselect_range    (SysprofSelection            *self,
                                                       gint64                       begin,
                                                       gint64                       end);
SYSPROF_AVAILABLE_IN_ALL
guint             sysprof_selection_get_n_ranges      (SysprofSelection            *self);
SYSPROF_AVAILABLE_IN_ALL
void              sysprof_selection_get_nth_range     (SysprofSelection            *self,
                                                       guint                        nth,
                                                       gint64                      *begin_time,
                                                       gint64                      *end_time);
SYSPROF_AVAILABLE_IN_ALL
void              sysprof_selection_unselect_all      (SysprofSelection            *self);
SYSPROF_AVAILABLE_IN_ALL
void              sysprof_selection_foreach           (SysprofSelection            *self,
                                                       SysprofSelectionForeachFunc  foreach_func,
                                                       gpointer                     user_data);
SYSPROF_AVAILABLE_IN_ALL
SysprofSelection *sysprof_selection_copy              (const SysprofSelection      *self);

G_END_DECLS
