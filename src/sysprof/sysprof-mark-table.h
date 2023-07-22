/* sysprof-mark-table.h
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

#include <gtk/gtk.h>

#include <sysprof.h>

#include "sysprof-session.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARK_TABLE (sysprof_mark_table_get_type())

G_DECLARE_FINAL_TYPE (SysprofMarkTable, sysprof_mark_table, SYSPROF, MARK_TABLE, GtkWidget)

GtkWidget      *sysprof_mark_table_new         (void);
SysprofSession *sysprof_mark_table_get_session (SysprofMarkTable *self);
void            sysprof_mark_table_set_session (SysprofMarkTable *self,
                                                SysprofSession   *session);

G_END_DECLS
