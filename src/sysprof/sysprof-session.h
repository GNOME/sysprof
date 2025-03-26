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

#include <sysprof.h>

#include "sysprof-axis.h"
#include "sysprof-time-span.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SESSION (sysprof_session_get_type())

G_DECLARE_FINAL_TYPE (SysprofSession, sysprof_session, SYSPROF, SESSION, GObject)

SysprofSession        *sysprof_session_new                    (SysprofDocument       *document);
SysprofDocument       *sysprof_session_get_document           (SysprofSession        *self);
const SysprofTimeSpan *sysprof_session_get_document_time      (SysprofSession        *self);
GtkFilter             *sysprof_session_get_filter             (SysprofSession        *self);
const SysprofTimeSpan *sysprof_session_get_selected_time      (SysprofSession        *self);
const SysprofTimeSpan *sysprof_session_get_visible_time       (SysprofSession        *self);
SysprofAxis           *sysprof_session_get_visible_time_axis  (SysprofSession        *self);
SysprofAxis           *sysprof_session_get_selected_time_axis (SysprofSession        *self);
void                   sysprof_session_select_time            (SysprofSession        *self,
                                                               const SysprofTimeSpan *time_span);
void                   sysprof_session_zoom_to_selection      (SysprofSession        *self);
void                   sysprof_session_filter_by_mark         (SysprofSession        *self,
                                                               SysprofMarkCatalog    *catalog);

G_END_DECLS
