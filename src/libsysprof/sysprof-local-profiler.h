/* sysprof-local-profiler.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-profiler.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_LOCAL_PROFILER (sysprof_local_profiler_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofLocalProfiler, sysprof_local_profiler, SYSPROF, LOCAL_PROFILER, GObject)

struct _SysprofLocalProfilerClass
{
  GObjectClass parent_class;
  gpointer     padding[8];
};

SYSPROF_AVAILABLE_IN_ALL
SysprofProfiler *sysprof_local_profiler_new        (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofProfiler *sysprof_local_profiler_new_replay (SysprofCaptureReader *reader);

G_END_DECLS
