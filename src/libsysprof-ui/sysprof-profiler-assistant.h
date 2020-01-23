/* sysprof-profiler-assistant.h
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

#include <gtk/gtk.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROFILER_ASSISTANT (sysprof_profiler_assistant_get_type())

G_DECLARE_FINAL_TYPE (SysprofProfilerAssistant, sysprof_profiler_assistant, SYSPROF, PROFILER_ASSISTANT, GtkBin)

GtkWidget *sysprof_profiler_assistant_new            (void);
void       sysprof_profiler_assistant_set_executable (SysprofProfilerAssistant *self,
                                                      const gchar              *path);

G_END_DECLS
