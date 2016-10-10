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
# include "sp-callgraph-view.h"
# include "sp-cell-renderer-percent.h"
# include "sp-cpu-visualizer-row.h"
# include "sp-failed-state-view.h"
# include "sp-line-visualizer-row.h"
# include "sp-empty-state-view.h"
# include "sp-model-filter.h"
# include "sp-multi-paned.h"
# include "sp-recording-state-view.h"
# include "sp-process-model.h"
# include "sp-process-model-item.h"
# include "sp-process-model-row.h"
# include "sp-profiler-menu-button.h"
# include "sp-visualizer-row.h"
# include "sp-visualizer-view.h"
# include "sp-zoom-manager.h"
#undef SYSPROF_INSIDE

G_END_DECLS

#endif /* SYSPROF_UI_H */
