/* sysprof-time-series-item.h
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

#include <glib-object.h>

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_SERIES_ITEM (sysprof_time_series_item_get_type())

G_DECLARE_FINAL_TYPE (SysprofTimeSeriesItem, sysprof_time_series_item, SYSPROF, TIME_SERIES_ITEM, GObject)

gint64   sysprof_time_series_item_get_begin_time     (SysprofTimeSeriesItem *self);
gint64   sysprof_time_series_item_get_duration       (SysprofTimeSeriesItem *self);
gint64   sysprof_time_series_item_get_end_time       (SysprofTimeSeriesItem *self);
gpointer sysprof_time_series_item_get_item           (SysprofTimeSeriesItem *self);

G_END_DECLS
