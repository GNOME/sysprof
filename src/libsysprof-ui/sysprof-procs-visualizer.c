/* sysprof-procs-visualizer.c
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

#define G_LOG_DOMAIN "sysprof-procs-visualizer"

#include "config.h"

#include <glib/gi18n.h>

#include "pointcache.h"
#include "sysprof-procs-visualizer.h"

typedef struct
{
  volatile gint         ref_count;
  guint                 n_procs;
  guint                 max_n_procs;
  gint64                begin_time;
  gint64                end_time;
  gint64                duration;
  PointCache           *cache;
  SysprofCaptureCursor *cursor;
} Discovery;

struct _SysprofProcsVisualizer
{
  SysprofVisualizer  parent_instance;
  Discovery         *discovery;
};

G_DEFINE_TYPE (SysprofProcsVisualizer, sysprof_procs_visualizer, SYSPROF_TYPE_VISUALIZER)

static void
discovery_unref (Discovery *d)
{
  if (g_atomic_int_dec_and_test (&d->ref_count))
    {
      g_clear_pointer (&d->cache, point_cache_unref);
      g_clear_pointer (&d->cursor, sysprof_capture_cursor_unref);
      g_slice_free (Discovery, d);
    }
}

static Discovery *
discovery_ref (Discovery *d)
{
  g_atomic_int_inc (&d->ref_count);
  return d;
}

static bool
discover_max_cb (const SysprofCaptureFrame *frame,
                 gpointer                   user_data)
{
  Discovery *d = user_data;

  g_assert (frame != NULL);
  g_assert (d != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_PROCESS)
    d->n_procs++;
  else if (frame->type == SYSPROF_CAPTURE_FRAME_EXIT)
    d->n_procs--;

  if (d->n_procs > d->max_n_procs)
    d->max_n_procs = d->n_procs;

  return TRUE;
}

static bool
calc_points_cb (const SysprofCaptureFrame *frame,
                gpointer                   user_data)
{
  Discovery *d = user_data;
  gdouble x, y;

  g_assert (frame != NULL);
  g_assert (d != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_PROCESS)
    d->n_procs++;
  else if (frame->type == SYSPROF_CAPTURE_FRAME_EXIT)
    d->n_procs--;

  x = (frame->time - d->begin_time) / (gdouble)d->duration;
  y = (d->n_procs / (gdouble)d->max_n_procs) * .85;

  point_cache_add_point_to_set (d->cache, 1, x, y);

  return TRUE;
}

static void
discovery_worker (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
  Discovery *d = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_PROCS_VISUALIZER (source_object));

  sysprof_capture_cursor_foreach (d->cursor, discover_max_cb, d);

  d->n_procs = 0;
  sysprof_capture_cursor_reset (d->cursor);

  sysprof_capture_cursor_foreach (d->cursor, calc_points_cb, d);

  g_task_return_pointer (task,
                         discovery_ref (d),
                         (GDestroyNotify) discovery_unref);
}

static void
handle_data_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  SysprofProcsVisualizer *self = (SysprofProcsVisualizer *)object;
  Discovery *d;

  g_assert (SYSPROF_IS_PROCS_VISUALIZER (self));
  g_assert (G_IS_TASK (result));

  if ((d = g_task_propagate_pointer (G_TASK (result), NULL)))
    {
      g_clear_pointer (&self->discovery, discovery_unref);
      self->discovery = g_steal_pointer (&d);
      gtk_widget_queue_allocate (GTK_WIDGET (self));
    }
}

static void
sysprof_procs_visualizer_set_reader (SysprofVisualizer    *visualizer,
                                     SysprofCaptureReader *reader)
{
  static const SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_PROCESS,
    SYSPROF_CAPTURE_FRAME_EXIT,
  };
  SysprofProcsVisualizer *self = (SysprofProcsVisualizer *)visualizer;
  g_autoptr(GTask) task = NULL;
  Discovery *d;

  g_assert (SYSPROF_IS_PROCS_VISUALIZER (self));
  g_assert (reader != NULL);

  d = g_slice_new0 (Discovery);
  d->ref_count = 1;
  d->cache = point_cache_new ();
  d->begin_time = sysprof_capture_reader_get_start_time (reader);
  d->end_time = sysprof_capture_reader_get_end_time (reader);
  d->cursor = sysprof_capture_cursor_new (reader);
  d->duration = d->end_time - d->begin_time;

  point_cache_add_set (d->cache, 1);
  sysprof_capture_cursor_add_condition (d->cursor,
                                        sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types));

  task = g_task_new (self, NULL, handle_data_cb, NULL);
  g_task_set_source_tag (task, sysprof_procs_visualizer_set_reader);
  g_task_set_task_data (task, d, (GDestroyNotify) discovery_unref);
  g_task_run_in_thread (task, discovery_worker);
}

static gboolean
sysprof_procs_visualizer_draw (GtkWidget *widget,
                               cairo_t   *cr)
{
  SysprofProcsVisualizer *self = (SysprofProcsVisualizer *)widget;
  g_autofree SysprofVisualizerAbsolutePoint *points = NULL;
  gboolean ret = GDK_EVENT_PROPAGATE;
  GtkAllocation alloc;
  GdkRGBA background;
  GdkRGBA foreground;
  const Point *fpoints;
  guint n_fpoints = 0;
  Discovery *d;
  gdouble last_x = 0;
  gdouble last_y = 0;

  g_assert (SYSPROF_IS_PROCS_VISUALIZER (self));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);

  gdk_rgba_parse (&foreground, "#813d9c");
  background = foreground;
  background.alpha *= .5;

  ret = GTK_WIDGET_CLASS (sysprof_procs_visualizer_parent_class)->draw (widget, cr);

  if (!(d = self->discovery) || d->cache == NULL)
    return ret;

  if (!(fpoints = point_cache_get_points (d->cache, 1, &n_fpoints)))
    return ret;

  points = g_new0 (SysprofVisualizerAbsolutePoint, n_fpoints);

  sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (self),
                                       (const SysprofVisualizerRelativePoint *)fpoints,
                                       n_fpoints,
                                       points,
                                       n_fpoints);

  last_x = points[0].x;
  last_y = points[0].y;

  cairo_move_to (cr, last_x, alloc.height);
  cairo_line_to (cr, last_x, last_y);

  for (guint i = 1; i < n_fpoints; i++)
    {
      cairo_curve_to (cr,
                      last_x + ((points[i].x - last_x) / 2),
                      last_y,
                      last_x + ((points[i].x - last_x) / 2),
                      points[i].y,
                      points[i].x,
                      points[i].y);

      last_x = points[i].x;
      last_y = points[i].y;
    }

  cairo_line_to (cr, last_x, alloc.height);
  cairo_close_path (cr);

  cairo_set_line_width (cr, 1.0);

  gdk_cairo_set_source_rgba (cr, &background);
  cairo_fill_preserve (cr);
  gdk_cairo_set_source_rgba (cr, &foreground);
  cairo_stroke (cr);

  return ret;
}

static void
sysprof_procs_visualizer_finalize (GObject *object)
{
  SysprofProcsVisualizer *self = (SysprofProcsVisualizer *)object;

  g_clear_pointer (&self->discovery, discovery_unref);

  G_OBJECT_CLASS (sysprof_procs_visualizer_parent_class)->finalize (object);
}

static void
sysprof_procs_visualizer_class_init (SysprofProcsVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofVisualizerClass *visualizer_class = SYSPROF_VISUALIZER_CLASS (klass);

  object_class->finalize = sysprof_procs_visualizer_finalize;

  widget_class->draw = sysprof_procs_visualizer_draw;

  visualizer_class->set_reader = sysprof_procs_visualizer_set_reader;
}

static void
sysprof_procs_visualizer_init (SysprofProcsVisualizer *self)
{
}

SysprofVisualizer *
sysprof_procs_visualizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROCS_VISUALIZER, NULL);
}
