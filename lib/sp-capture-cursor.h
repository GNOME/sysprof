/* sp-capture-cursor.h
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_CAPTURE_CURSOR_H
#define SP_CAPTURE_CURSOR_H

#include "sp-capture-types.h"

G_BEGIN_DECLS

#define SP_TYPE_CAPTURE_CURSOR (sp_capture_cursor_get_type())

G_DECLARE_FINAL_TYPE (SpCaptureCursor, sp_capture_cursor, SP, CAPTURE_CURSOR, GObject)

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

SpCaptureCursor *sp_capture_cursor_new            (SpCaptureReader         *reader);
void             sp_capture_cursor_foreach        (SpCaptureCursor         *self,
                                                   SpCaptureCursorCallback  callback,
                                                   gpointer                 user_data);
void             sp_capture_cursor_reset          (SpCaptureCursor         *self);
void             sp_capture_cursor_reverse        (SpCaptureCursor         *self);
void             sp_capture_cursor_add_condition  (SpCaptureCursor         *self,
                                                   SpCaptureCondition      *condition);

G_END_DECLS

#endif /* SP_CAPTURE_CURSOR_H */
