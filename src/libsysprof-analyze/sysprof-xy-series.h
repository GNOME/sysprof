/* sysprof-xy-series.h
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

#include <sysprof-capture.h>

#include <gio/gio.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_XY_SERIES (sysprof_xy_series_get_type())

typedef struct _SysprofXYSeries SysprofXYSeries;
typedef struct _SysprofXYSeriesValue SysprofXYSeriesValue;

struct _SysprofXYSeriesValue
{
  float x;
  float y;
  guint index;
};

SYSPROF_AVAILABLE_IN_ALL
GType                       sysprof_xy_series_get_type   (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofXYSeries            *sysprof_xy_series_new        (GListModel            *model,
                                                          float                  min_x,
                                                          float                  min_y,
                                                          float                  max_x,
                                                          float                  max_y);
SYSPROF_AVAILABLE_IN_ALL
SysprofXYSeries            *sysprof_xy_series_ref        (SysprofXYSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
void                        sysprof_xy_series_unref      (SysprofXYSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
void                        sysprof_xy_series_add        (SysprofXYSeries       *self,
                                                          float                  x,
                                                          float                  y,
                                                          guint                  index);
SYSPROF_AVAILABLE_IN_ALL
void                        sysprof_xy_series_sort       (SysprofXYSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel                 *sysprof_xy_series_get_model  (SysprofXYSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofXYSeriesValue *sysprof_xy_series_get_values (const SysprofXYSeries *self,
                                                          guint                 *n_values);
SYSPROF_AVAILABLE_IN_ALL
void                        sysprof_xy_series_get_range  (SysprofXYSeries       *self,
                                                          float                 *min_x,
                                                          float                 *min_y,
                                                          float                 *max_x,
                                                          float                 *max_y);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofXYSeries, sysprof_xy_series_unref)

G_END_DECLS
