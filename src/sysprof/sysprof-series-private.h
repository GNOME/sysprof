/* sysprof-series-private.h
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

#include "sysprof-series.h"

G_BEGIN_DECLS

#define SYSPROF_SERIES_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, SYSPROF_TYPE_SERIES, SysprofSeriesClass)

struct _SysprofSeries
{
  GObject     parent_instance;
  char       *title;
  GListModel *model;
  gulong      items_changed_handler;
};

struct _SysprofSeriesClass
{
  GObjectClass parent_class;

  GType series_item_type;

  gpointer (*get_series_item) (SysprofSeries *self,
                               guint          position,
                               gpointer       item);
  void     (*items_changed)   (SysprofSeries *self,
                               GListModel    *model,
                               guint          position,
                               guint          removed,
                               guint          added);
};

G_END_DECLS
