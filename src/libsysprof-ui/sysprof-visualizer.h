/* sysprof-visualizer.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <dazzle.h>
#include <sysprof.h>

G_BEGIN_DECLS

typedef struct
{
  gdouble x;
  gdouble y;
} SysprofVisualizerRelativePoint;

typedef struct
{
  gint x;
  gint y;
} SysprofVisualizerAbsolutePoint;

#define SYSPROF_TYPE_VISUALIZER (sysprof_visualizer_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofVisualizer, sysprof_visualizer, SYSPROF, VISUALIZER, DzlBin)

struct _SysprofVisualizerClass
{
  DzlBinClass parent_class;

  void (*set_reader) (SysprofVisualizer    *self,
                      SysprofCaptureReader *reader);

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
const gchar *sysprof_visualizer_get_title        (SysprofVisualizer                    *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_visualizer_set_title        (SysprofVisualizer                    *self,
                                                  const gchar                          *title);
SYSPROF_AVAILABLE_IN_ALL
gint64       sysprof_visualizer_get_begin_time   (SysprofVisualizer                    *self);
SYSPROF_AVAILABLE_IN_ALL
gint64       sysprof_visualizer_get_end_time     (SysprofVisualizer                    *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_visualizer_set_time_range   (SysprofVisualizer                    *self,
                                                  gint64                                begin_time,
                                                  gint64                                end_time);
SYSPROF_AVAILABLE_IN_ALL
gint64       sysprof_visualizer_get_duration     (SysprofVisualizer                    *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_visualizer_set_reader       (SysprofVisualizer                    *self,
                                                  SysprofCaptureReader                 *reader);
SYSPROF_AVAILABLE_IN_ALL
gint         sysprof_visualizer_get_x_for_time   (SysprofVisualizer                    *self,
                                                  gint64                                time);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_visualizer_translate_points (SysprofVisualizer                    *self,
                                                  const SysprofVisualizerRelativePoint *in_points,
                                                  guint                                 n_in_points,
                                                  SysprofVisualizerAbsolutePoint       *out_points,
                                                  guint                                 n_out_points);

G_END_DECLS
