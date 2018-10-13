/* sysprof.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef SYSPROF_H
#define SYSPROF_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SYSPROF_INSIDE
#define SP_ENABLE_GOBJECT

# include "sp-address.h"
# include "sp-clock.h"
# include "sp-error.h"
# include "sysprof-version.h"

# include "callgraph/sp-callgraph-profile.h"

# include "capture/sp-capture-condition.h"
# include "capture/sp-capture-cursor.h"
# include "capture/sp-capture-reader.h"
# include "capture/sp-capture-writer.h"

# include "profiler/sp-local-profiler.h"
# include "profiler/sp-profile.h"
# include "profiler/sp-profiler.h"

# include "sources/sp-gjs-source.h"
# include "sources/sp-hostinfo-source.h"
# include "sources/sp-memory-source.h"
# include "sources/sp-perf-source.h"
# include "sources/sp-proc-source.h"
# include "sources/sp-source.h"

# include "symbols/sp-elf-symbol-resolver.h"
# include "symbols/sp-jitmap-symbol-resolver.h"
# include "symbols/sp-kernel-symbol-resolver.h"
# include "symbols/sp-kernel-symbol.h"
# include "symbols/sp-symbol-dirs.h"
# include "symbols/sp-symbol-resolver.h"

# include "util/sp-map-lookaside.h"
# include "util/sp-selection.h"

#undef SP_ENABLE_GOBJECT
#undef SYSPROF_INSIDE

G_END_DECLS

#endif /* SYSPROF_H */
