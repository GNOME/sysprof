/* sysprof-zoom-manager.h
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <glib-object.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_ZOOM_MANAGER (sysprof_zoom_manager_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofZoomManager, sysprof_zoom_manager, SYSPROF, ZOOM_MANAGER, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofZoomManager *sysprof_zoom_manager_new                    (void);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_zoom_manager_get_can_zoom_in        (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_zoom_manager_get_can_zoom_out       (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_zoom_manager_get_min_zoom           (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_zoom_manager_get_max_zoom           (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_set_min_zoom           (SysprofZoomManager *self,
                                                                 gdouble             min_zoom);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_set_max_zoom           (SysprofZoomManager *self,
                                                                 gdouble             max_zoom);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_zoom_in                (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_zoom_out               (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_reset                  (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gdouble             sysprof_zoom_manager_get_zoom               (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_zoom_manager_set_zoom               (SysprofZoomManager *self,
                                                                 gdouble             zoom);
SYSPROF_AVAILABLE_IN_ALL
gchar              *sysprof_zoom_manager_get_zoom_label         (SysprofZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gint                sysprof_zoom_manager_get_width_for_duration (SysprofZoomManager *self,
                                                                 gint64              duration);
SYSPROF_AVAILABLE_IN_ALL
gint64              sysprof_zoom_manager_get_duration_for_width (SysprofZoomManager *self,
                                                                 gint                width);
SYSPROF_AVAILABLE_IN_ALL
gdouble             sysprof_zoom_manager_fit_zoom_for_duration  (SysprofZoomManager *self,
                                                                 gint64              duration,
                                                                 gint                width);

G_END_DECLS
