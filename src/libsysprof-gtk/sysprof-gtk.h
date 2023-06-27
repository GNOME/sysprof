/* sysprof-gtk.h
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

G_BEGIN_DECLS

#define SYSPROF_GTK_INSIDE
# include "sysprof-axis.h"
# include "sysprof-callgraph-view.h"
# include "sysprof-chart.h"
# include "sysprof-chart-layer.h"
# include "sysprof-column-layer.h"
# include "sysprof-duplex-layer.h"
# include "sysprof-line-layer.h"
# include "sysprof-mark-chart.h"
# include "sysprof-mark-table.h"
# include "sysprof-normalized-series.h"
# include "sysprof-normalized-series-item.h"
# include "sysprof-series.h"
# include "sysprof-session.h"
# include "sysprof-session-model.h"
# include "sysprof-session-model-item.h"
# include "sysprof-split-layer.h"
# include "sysprof-time-series.h"
# include "sysprof-time-series-item.h"
# include "sysprof-time-span-layer.h"
# include "sysprof-value-axis.h"
# include "sysprof-weighted-callgraph-view.h"
# include "sysprof-xy-layer.h"
# include "sysprof-xy-series.h"
# include "sysprof-xy-series-item.h"
#undef SYSPROF_GTK_INSIDE

G_END_DECLS

