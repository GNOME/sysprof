/* sysprof-ui.h
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

#ifndef SYSPROF_UI_H
#define SYSPROF_UI_H

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_INSIDE
# include "callgraph/sp-callgraph-view.h"
# include "widgets/sp-cell-renderer-percent.h"
# include "visualizers/sp-cpu-visualizer-row.h"
# include "widgets/sp-failed-state-view.h"
# include "visualizers/sp-line-visualizer-row.h"
# include "widgets/sp-empty-state-view.h"
# include "util/sp-model-filter.h"
# include "widgets/sp-multi-paned.h"
# include "widgets/sp-recording-state-view.h"
# include "util/sp-process-model.h"
# include "util/sp-process-model-item.h"
# include "widgets/sp-process-model-row.h"
# include "widgets/sp-profiler-menu-button.h"
# include "visualizers/sp-visualizer-row.h"
# include "visualizers/sp-visualizer-view.h"
# include "util/sp-zoom-manager.h"
#undef SYSPROF_INSIDE

G_END_DECLS

#endif /* SYSPROF_UI_H */
