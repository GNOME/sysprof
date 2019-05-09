/* sysprof-theme-manager.h
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_THEME_MANAGER (sysprof_theme_manager_get_type())

G_GNUC_INTERNAL
G_DECLARE_FINAL_TYPE (SysprofThemeManager, sysprof_theme_manager, SYSPROF, THEME_MANAGER, GObject)

G_GNUC_INTERNAL
SysprofThemeManager *sysprof_theme_manager_get_default       (void);
G_GNUC_INTERNAL
void                 sysprof_theme_manager_unregister        (SysprofThemeManager *self,
                                                              guint                registration_id);
G_GNUC_INTERNAL
guint                sysprof_theme_manager_register_resource (SysprofThemeManager *self,
                                                              const gchar         *theme_name,
                                                              const gchar         *variant,
                                                              const gchar         *resource);

G_END_DECLS
