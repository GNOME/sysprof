/* sp-zoom-manager.h
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

#define SP_TYPE_ZOOM_MANAGER (sp_zoom_manager_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpZoomManager, sp_zoom_manager, SP, ZOOM_MANAGER, GObject)

SYSPROF_AVAILABLE_IN_ALL
SpZoomManager *sp_zoom_manager_new              (void);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sp_zoom_manager_get_can_zoom_in  (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sp_zoom_manager_get_can_zoom_out (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sp_zoom_manager_get_min_zoom     (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sp_zoom_manager_get_max_zoom     (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_set_min_zoom     (SpZoomManager *self,
                                                 gdouble        min_zoom);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_set_max_zoom     (SpZoomManager *self,
                                                 gdouble        max_zoom);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_zoom_in          (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_zoom_out         (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_reset            (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
gdouble        sp_zoom_manager_get_zoom         (SpZoomManager *self);
SYSPROF_AVAILABLE_IN_ALL
void           sp_zoom_manager_set_zoom         (SpZoomManager *self,
                                                 gdouble        zoom);

G_END_DECLS
