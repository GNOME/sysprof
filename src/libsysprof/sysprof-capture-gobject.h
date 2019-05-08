/* sysprof-capture-gobject.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define SYSPROF_TYPE_CAPTURE_READER (sysprof_capture_reader_get_type())
#define SYSPROF_TYPE_CAPTURE_WRITER (sysprof_capture_writer_get_type())
#define SYSPROF_TYPE_CAPTURE_CURSOR (sysprof_capture_cursor_get_type())

SYSPROF_AVAILABLE_IN_ALL
GType sysprof_capture_reader_get_type (void);
SYSPROF_AVAILABLE_IN_ALL
GType sysprof_capture_writer_get_type (void);
SYSPROF_AVAILABLE_IN_ALL
GType sysprof_capture_cursor_get_type (void);

G_END_DECLS
