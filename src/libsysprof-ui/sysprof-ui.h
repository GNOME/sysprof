/* sysprof-ui.h
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_UI_INSIDE

# include "sysprof-callgraph-view.h"
# include "sysprof-cell-renderer-percent.h"
# include "sysprof-cpu-visualizer-row.h"
# include "sysprof-empty-state-view.h"
# include "sysprof-failed-state-view.h"
# include "sysprof-line-visualizer-row.h"
# include "sysprof-marks-model.h"
# include "sysprof-marks-view.h"
# include "sysprof-mark-visualizer-row.h"
# include "sysprof-model-filter.h"
# include "sysprof-multi-paned.h"
# include "sysprof-process-model-row.h"
# include "sysprof-profiler-menu-button.h"
# include "sysprof-recording-state-view.h"
# include "sysprof-visualizer-row.h"
# include "sysprof-visualizer-view.h"
# include "sysprof-zoom-manager.h"

#undef SYSPROF_UI_INSIDE

G_END_DECLS
