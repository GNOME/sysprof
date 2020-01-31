/* sysprof.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#pragma once

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_INSIDE

# include "sysprof-battery-source.h"
# include "sysprof-callgraph-profile.h"
# include "sysprof-capture-gobject.h"
# include "sysprof-capture-symbol-resolver.h"
# include "sysprof-diskstat-source.h"
# include "sysprof-elf-symbol-resolver.h"
# include "sysprof-gjs-source.h"
# include "sysprof-governor-source.h"
# include "sysprof-jitmap-symbol-resolver.h"
# include "sysprof-kernel-symbol-resolver.h"
# include "sysprof-kernel-symbol.h"
# include "sysprof-local-profiler.h"
# include "sysprof-memprof-profile.h"
# include "sysprof-memprof-source.h"
# include "sysprof-netdev-source.h"
# include "sysprof-process-model-item.h"
# include "sysprof-process-model.h"
# include "sysprof-profile.h"
# include "sysprof-profiler.h"
# include "sysprof-proxy-source.h"
# include "sysprof-selection.h"
# include "sysprof-source.h"
# include "sysprof-spawnable.h"
# include "sysprof-symbol-resolver.h"
# include "sysprof-symbols-source.h"
# include "sysprof-tracefd-source.h"

#ifdef __linux__
# include "sysprof-hostinfo-source.h"
# include "sysprof-memory-source.h"
# include "sysprof-perf-source.h"
# include "sysprof-proc-source.h"
#endif

#undef SYSPROF_INSIDE

G_END_DECLS
