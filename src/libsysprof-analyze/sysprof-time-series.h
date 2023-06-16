/* sysprof-time-series.h
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

#include "sysprof-time-span.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_SERIES (sysprof_time_series_get_type())

typedef struct _SysprofTimeSeries SysprofTimeSeries;
typedef struct _SysprofTimeSeriesValue SysprofTimeSeriesValue;

struct _SysprofTimeSeriesValue
{
  /* Normalized begin/end value between 0..1 */
  union {
    float time[2];
    struct {
      float begin;
      float end;
    };
  };

  /* Index of SysprofDocumentFrame */
  guint index;
};

SYSPROF_AVAILABLE_IN_ALL
GType                         sysprof_time_series_get_type   (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofTimeSeries            *sysprof_time_series_new        (GListModel             *model,
                                                              SysprofTimeSpan         time_span);
SYSPROF_AVAILABLE_IN_ALL
SysprofTimeSeries            *sysprof_time_series_ref        (SysprofTimeSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
void                          sysprof_time_series_unref      (SysprofTimeSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
void                          sysprof_time_series_add        (SysprofTimeSeries       *self,
                                                              SysprofTimeSpan          time_span,
                                                              guint                    index);
SYSPROF_AVAILABLE_IN_ALL
void                          sysprof_time_series_sort       (SysprofTimeSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
GListModel                   *sysprof_time_series_get_model  (SysprofTimeSeries       *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofTimeSeriesValue *sysprof_time_series_get_values (const SysprofTimeSeries *self,
                                                              guint                   *n_values);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofTimeSeries, sysprof_time_series_unref)

G_END_DECLS
