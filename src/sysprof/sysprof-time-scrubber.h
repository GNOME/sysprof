/* sysprof-time-scrubber.h
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

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_SCRUBBER (sysprof_time_scrubber_get_type())

G_DECLARE_FINAL_TYPE (SysprofTimeScrubber, sysprof_time_scrubber, SYSPROF, TIME_SCRUBBER, GtkWidget)

GtkWidget      *sysprof_time_scrubber_new         (void);
SysprofSession *sysprof_time_scrubber_get_session (SysprofTimeScrubber *self);
void            sysprof_time_scrubber_set_session (SysprofTimeScrubber *self,
                                                   SysprofSession      *session);
void            sysprof_time_scrubber_add_chart   (SysprofTimeScrubber *self,
                                                   GtkWidget           *chart);

G_END_DECLS
