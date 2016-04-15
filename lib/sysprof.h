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
# include "sp-address.h"
# include "sp-callgraph-profile.h"
# include "sp-capture-reader.h"
# include "sp-capture-writer.h"
# include "sp-clock.h"
# include "sp-elf-symbol-resolver.h"
# include "sp-error.h"
# include "sp-gjs-source.h"
# include "sp-hostinfo-source.h"
# include "sp-jitmap-symbol-resolver.h"
# include "sp-kernel-symbol-resolver.h"
# include "sp-kernel-symbol.h"
# include "sp-local-profiler.h"
# include "sp-map-lookaside.h"
# include "sp-perf-source.h"
# include "sp-proc-source.h"
# include "sp-profile.h"
# include "sp-profiler.h"
# include "sp-source.h"
# include "sp-symbol-resolver.h"
# include "sysprof-version.h"
#undef SYSPROF_INSIDE

G_END_DECLS

#endif /* SYSPROF_H */
