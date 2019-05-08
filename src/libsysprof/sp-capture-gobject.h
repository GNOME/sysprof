/* sp-capture-gobject.h
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
#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SP_TYPE_CAPTURE_READER (sp_capture_reader_get_type())
#define SP_TYPE_CAPTURE_WRITER (sp_capture_writer_get_type())
#define SP_TYPE_CAPTURE_CURSOR (sp_capture_cursor_get_type())

GType sp_capture_reader_get_type (void);
GType sp_capture_writer_get_type (void);
GType sp_capture_cursor_get_type (void);

G_END_DECLS
