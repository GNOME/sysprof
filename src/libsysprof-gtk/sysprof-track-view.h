/* sysprof-track-view.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <gtk/gtk.h>

#include "sysprof-track.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_TRACK_VIEW (sysprof_track_view_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofTrackView, sysprof_track_view, SYSPROF, TRACK_VIEW, GtkWidget)

SYSPROF_AVAILABLE_IN_ALL
GtkWidget    *sysprof_track_view_new       (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofTrack *sysprof_track_view_get_track (SysprofTrackView *self);
SYSPROF_AVAILABLE_IN_ALL
void          sysprof_track_view_set_track (SysprofTrackView *self,
                                            SysprofTrack     *track);

G_END_DECLS
