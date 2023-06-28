/* sysprof-time-ruler.h
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

#include "sysprof-session.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_RULER (sysprof_time_ruler_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofTimeRuler, sysprof_time_ruler, SYSPROF, TIME_RULER, GtkWidget)

SYSPROF_AVAILABLE_IN_ALL
GtkWidget      *sysprof_time_ruler_new         (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofSession *sysprof_time_ruler_get_session (SysprofTimeRuler *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_time_ruler_set_session (SysprofTimeRuler *self,
                                                SysprofSession   *session);

G_END_DECLS
