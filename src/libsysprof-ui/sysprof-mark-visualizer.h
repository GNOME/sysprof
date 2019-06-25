/* sysprof-mark-visualizer.h
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-visualizer.h"

G_BEGIN_DECLS

typedef struct
{
  gint64 begin;
  gint64 end;
  guint  kind;
  gint   x;
  gint   x2;
} SysprofMarkTimeSpan;

#define SYSPROF_TYPE_MARK_VISUALIZER (sysprof_mark_visualizer_get_type())

G_DECLARE_FINAL_TYPE (SysprofMarkVisualizer, sysprof_mark_visualizer, SYSPROF, MARK_VISUALIZER, SysprofVisualizer)

SysprofVisualizer *sysprof_mark_visualizer_new            (GHashTable            *groups);
void               sysprof_mark_visualizer_set_group_rgba (SysprofMarkVisualizer *self,
                                                           const gchar           *group,
                                                           const GdkRGBA         *rgba);
void               sysprof_mark_visualizer_set_kind_rgba  (SysprofMarkVisualizer *self,
                                                           GHashTable            *rgba_by_kind);

G_END_DECLS
