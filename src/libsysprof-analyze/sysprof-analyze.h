/* sysprof-analyze.h
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

#include <glib.h>

G_BEGIN_DECLS

#define SYSPROF_ANALYZE_INSIDE
# include "sysprof-bundled-symbolizer.h"
# include "sysprof-document.h"
# include "sysprof-document-allocation.h"
# include "sysprof-document-exit.h"
# include "sysprof-document-file.h"
# include "sysprof-document-file-chunk.h"
# include "sysprof-document-fork.h"
# include "sysprof-document-frame.h"
# include "sysprof-document-log.h"
# include "sysprof-document-mark.h"
# include "sysprof-document-metadata.h"
# include "sysprof-document-mmap.h"
# include "sysprof-document-process.h"
# include "sysprof-document-sample.h"
# include "sysprof-document-symbols.h"
# include "sysprof-document-traceable.h"
# include "sysprof-multi-symbolizer.h"
# include "sysprof-symbol.h"
# include "sysprof-symbolizer.h"
#undef SYSPROF_ANALYZE_INSIDE

G_END_DECLS
