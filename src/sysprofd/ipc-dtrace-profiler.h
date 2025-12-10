/* ipc-dtrace-profiler.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#ifdef __FreeBSD__

#include "ipc-profiler.h"

G_BEGIN_DECLS

#define IPC_TYPE_DTRACE_PROFILER (ipc_dtrace_profiler_get_type())

G_DECLARE_FINAL_TYPE (IpcDtraceProfiler, ipc_dtrace_profiler, IPC, DTRACE_PROFILER, IpcProfilerSkeleton)

IpcProfiler *ipc_dtrace_profiler_new (void);

G_END_DECLS

#endif /* __FreeBSD__ */
