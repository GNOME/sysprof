/* sp-capture-cursor.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "sp-capture-types.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef struct _SpCaptureCursor SpCaptureCursor;

/**
 * SpCaptureCursorCallback:
 *
 * This is the prototype for callbacks that are called for each frame
 * matching the cursor query.
 *
 * Functions matching this typedef should return %TRUE if they want the
 * the caller to stop iteration of cursor.
 *
 * Returns: %TRUE if iteration should stop, otherwise %FALSE.
 */
typedef gboolean (*SpCaptureCursorCallback) (const SpCaptureFrame *frame,
                                             gpointer              user_data);

SYSPROF_AVAILABLE_IN_ALL
SpCaptureCursor *sp_capture_cursor_new            (SpCaptureReader         *reader);
SYSPROF_AVAILABLE_IN_ALL
void             sp_capture_cursor_unref          (SpCaptureCursor         *self);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureCursor *sp_capture_cursor_ref            (SpCaptureCursor         *self);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureReader *sp_capture_cursor_get_reader     (SpCaptureCursor         *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_capture_cursor_foreach        (SpCaptureCursor         *self,
                                                   SpCaptureCursorCallback  callback,
                                                   gpointer                 user_data);
SYSPROF_AVAILABLE_IN_ALL
void             sp_capture_cursor_reset          (SpCaptureCursor         *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_capture_cursor_reverse        (SpCaptureCursor         *self);
SYSPROF_AVAILABLE_IN_ALL
void             sp_capture_cursor_add_condition  (SpCaptureCursor         *self,
                                                   SpCaptureCondition      *condition);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpCaptureCursor, sp_capture_cursor_unref)

G_END_DECLS
