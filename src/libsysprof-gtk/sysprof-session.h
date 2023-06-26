/* sysprof-session.h
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

#include <sysprof-analyze.h>

#include "sysprof-axis.h"
#include "sysprof-time-span.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SESSION (sysprof_session_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSession, sysprof_session, SYSPROF, SESSION, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSession        *sysprof_session_new                    (SysprofDocument       *document);
SYSPROF_AVAILABLE_IN_ALL
SysprofDocument       *sysprof_session_get_document           (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
GtkFilter             *sysprof_session_get_filter             (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofTimeSpan *sysprof_session_get_selected_time      (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofTimeSpan *sysprof_session_get_visible_time       (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofAxis           *sysprof_session_get_visible_time_axis  (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofAxis           *sysprof_session_get_selected_time_axis (SysprofSession        *self);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_session_select_time            (SysprofSession        *self,
                                                               const SysprofTimeSpan *time_span);

G_END_DECLS
