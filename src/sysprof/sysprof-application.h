/* sysprof-application.h
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_APPLICATION    (sysprof_application_get_type())
#define SYSPROF_APPLICATION_DEFAULT (SYSPROF_APPLICATION(g_application_get_default()))

G_DECLARE_FINAL_TYPE (SysprofApplication, sysprof_application, SYSPROF, APPLICATION, AdwApplication)

SysprofApplication *sysprof_application_new (void);

G_END_DECLS
