/* sysprof-capture-gobject.h
 *
 * Copyright 2020 Endless Mobile, Inc.
 *
 * Author:
 *  - Philip Withnall <withnall@endlessm.com>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include <glib.h>

#include "sysprof-capture.h"

G_BEGIN_DECLS

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureCondition, sysprof_capture_condition_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureCursor, sysprof_capture_cursor_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

G_END_DECLS
