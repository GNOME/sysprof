/* sysprof-source-location.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include <gio/gio.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SOURCE_LOCATION (sysprof_source_location_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSourceLocation, sysprof_source_location, SYSPROF, SOURCE_LOCATION, GObject)

SYSPROF_AVAILABLE_IN_ALL
GFile *sysprof_source_location_dup_file      (SysprofSourceLocation *self);
SYSPROF_AVAILABLE_IN_ALL
guint  sysprof_source_location_get_character (SysprofSourceLocation *self);
SYSPROF_AVAILABLE_IN_ALL
guint  sysprof_source_location_get_line      (SysprofSourceLocation *self);

G_END_DECLS
