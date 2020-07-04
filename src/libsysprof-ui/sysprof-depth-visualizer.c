/* sysprof-depth-visualizer.c
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

#define G_LOG_DOMAIN "sysprof-depth-visualizer"

#include "config.h"

#include <glib/gi18n.h>

#include "pointcache.h"
#include "sysprof-depth-visualizer.h"

struct _SysprofDepthVisualizer
{
  SysprofVisualizer     parent_instance;
  SysprofCaptureReader *reader;
  PointCache           *points;
  guint                 reload_source;
  guint                 mode;
  GtkAllocation         last_alloc;
  guint                 reloading : 1;
  guint                 needs_reload : 1;
};

typedef struct
{
  SysprofCaptureReader *reader;
  PointCache           *pc;
  gint64                begin_time;
  gint64                end_time;
  gint64                duration;
  guint                 max_n_addrs;
  guint                 mode;
} State;

static void sysprof_depth_visualizer_reload (SysprofDepthVisualizer *self);

G_DEFINE_TYPE (SysprofDepthVisualizer, sysprof_depth_visualizer, SYSPROF_TYPE_VISUALIZER)

static void
state_free (State *st)
{
  g_clear_pointer (&st->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&st->pc, point_cache_unref);
  g_slice_free (State, st);
}

static bool
discover_max_n_addr (const SysprofCaptureFrame *frame,
                     gpointer                   user_data)
{
  const SysprofCaptureSample *sample = (const SysprofCaptureSample *)frame;
  State *st = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE);
  g_assert (st != NULL);

  st->max_n_addrs = MAX (st->max_n_addrs, sample->n_addrs);

  return TRUE;
}

static bool
build_point_cache_cb (const SysprofCaptureFrame *frame,
                      gpointer                   user_data)
{
  const SysprofCaptureSample *sample = (const SysprofCaptureSample *)frame;
  State *st = user_data;
  gdouble x, y;
  gboolean has_kernel = FALSE;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE);
  g_assert (st != NULL);

  x = (frame->time - st->begin_time) / (gdouble)st->duration;
  y = sample->n_addrs / (gdouble)st->max_n_addrs;

  /* If this contains a context-switch (meaning we're going into the kernel
   * to do some work, use a negative value for Y so that we know later on
   * that we should draw it with a different color (after removing the negation
   * on the value.
   *
   * We skip past the first index, which is always a context switch as it is
   * our perf handler.
   */
  for (guint i = 1; i < sample->n_addrs; i++)
    {
      SysprofAddressContext kind;

      if (sysprof_address_is_context_switch (sample->addrs[i], &kind))
        {
          has_kernel = TRUE;
          y = -y;
          break;
        }
    }

  if (!has_kernel)
    point_cache_add_point_to_set (st->pc, 1, x, y);
  else
    point_cache_add_point_to_set (st->pc, 2, x, y);

  return TRUE;
}

static void
sysprof_depth_visualizer_worker (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  static const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_SAMPLE, };
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  SysprofCaptureCondition *condition;
  State *st = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (source_object));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (st->duration != 0)
    {
      cursor = sysprof_capture_cursor_new (st->reader);
      condition = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
      sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

      sysprof_capture_cursor_foreach (cursor, discover_max_n_addr, st);
      sysprof_capture_cursor_reset (cursor);
      sysprof_capture_cursor_foreach (cursor, build_point_cache_cb, st);
    }

  g_task_return_pointer (task,
                         g_steal_pointer (&st->pc),
                         (GDestroyNotify) point_cache_unref);
}

static void
apply_point_cache_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  SysprofDepthVisualizer *self = (SysprofDepthVisualizer *)object;
  PointCache *pc;

  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (self));
  g_assert (G_IS_TASK (result));

  self->reloading = FALSE;

  if ((pc = g_task_propagate_pointer (G_TASK (result), NULL)))
    {
      g_clear_pointer (&self->points, point_cache_unref);
      self->points = g_steal_pointer (&pc);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }

  if (self->needs_reload)
    sysprof_depth_visualizer_reload (self);
}

static void
sysprof_depth_visualizer_reload (SysprofDepthVisualizer *self)
{
  g_autoptr(GTask) task = NULL;
  GtkAllocation alloc;
  State *st;

  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (self));

  self->needs_reload = TRUE;

  if (self->reloading)
    return;

  self->reloading = TRUE;
  self->needs_reload = FALSE;

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  st = g_slice_new0 (State);
  st->reader = sysprof_capture_reader_ref (self->reader);
  st->pc = point_cache_new ();
  st->max_n_addrs = 0;
  st->begin_time = sysprof_capture_reader_get_start_time (self->reader);
  st->end_time = sysprof_capture_reader_get_end_time (self->reader);
  st->duration = st->end_time - st->begin_time;
  st->mode = self->mode;

  point_cache_add_set (st->pc, 1);
  point_cache_add_set (st->pc, 2);

  task = g_task_new (self, NULL, apply_point_cache_cb, NULL);
  g_task_set_source_tag (task, sysprof_depth_visualizer_reload);
  g_task_set_task_data (task, st, (GDestroyNotify) state_free);
  g_task_run_in_thread (task, sysprof_depth_visualizer_worker);
}

static void
sysprof_depth_visualizer_set_reader (SysprofVisualizer    *row,
                                     SysprofCaptureReader *reader)
{
  SysprofDepthVisualizer *self = (SysprofDepthVisualizer *)row;

  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (self));

  if (self->reader != reader)
    {
      if (self->reader != NULL)
        {
          sysprof_capture_reader_unref (self->reader);
          self->reader = NULL;
        }

      if (reader != NULL)
        {
          self->reader = sysprof_capture_reader_ref (reader);
          sysprof_depth_visualizer_reload (self);
        }
    }
}

static gboolean
sysprof_depth_visualizer_draw (GtkWidget *widget,
                               cairo_t   *cr)
{
  SysprofDepthVisualizer *self = (SysprofDepthVisualizer *)widget;
  GtkAllocation alloc;
  GdkRectangle clip;
  const Point *points;
  gboolean ret;
  guint n_points = 0;
  GdkRGBA user;
  GdkRGBA system;

  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (self));
  g_assert (cr != NULL);

  ret = GTK_WIDGET_CLASS (sysprof_depth_visualizer_parent_class)->draw (widget, cr);

  if (self->points == NULL)
    return ret;

  gdk_rgba_parse (&user, "#1a5fb4");
  gdk_rgba_parse (&system, "#3584e4");

  gtk_widget_get_allocation (widget, &alloc);

  if (!gdk_cairo_get_clip_rectangle (cr, &clip))
    return ret;

  /* Draw user-space stacks */
  if (self->mode != SYSPROF_DEPTH_VISUALIZER_KERNEL_ONLY &&
      (points = point_cache_get_points (self->points, 1, &n_points)))
    {
      g_autofree SysprofVisualizerAbsolutePoint *out_points = NULL;

      out_points = g_new (SysprofVisualizerAbsolutePoint, n_points);
      sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (widget),
                                           (const SysprofVisualizerRelativePoint *)points,
                                           n_points, out_points, n_points);

      cairo_set_line_width (cr, 1.0);
      gdk_cairo_set_source_rgba (cr, &user);

      for (guint i = 0; i < n_points; i++)
        {
          gdouble x, y;

          x = out_points[i].x;
          y = out_points[i].y;

          if (x < clip.x)
            continue;

          if (x > clip.x + clip.width)
            break;

          for (guint j = i + 1; j < n_points; j++)
            {
              if (out_points[j].x != x)
                break;

              y = MIN (y, out_points[j].y);
            }

          x += alloc.x;

          cairo_move_to (cr, (guint)x + .5, alloc.height);
          cairo_line_to (cr, (guint)x + .5, y);
        }

      cairo_stroke (cr);
    }

  /* Draw kernel-space stacks */
  if (self->mode != SYSPROF_DEPTH_VISUALIZER_USER_ONLY &&
      (points = point_cache_get_points (self->points, 2, &n_points)))
    {
      g_autofree SysprofVisualizerAbsolutePoint *out_points = NULL;

      out_points = g_new (SysprofVisualizerAbsolutePoint, n_points);
      sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (widget),
                                           (const SysprofVisualizerRelativePoint *)points,
                                           n_points, out_points, n_points);

      cairo_set_line_width (cr, 1.0);
      gdk_cairo_set_source_rgba (cr, &system);

      for (guint i = 0; i < n_points; i++)
        {
          gdouble x, y;

          x = out_points[i].x;
          y = out_points[i].y;

          if (x < clip.x)
            continue;

          if (x > clip.x + clip.width)
            break;

          for (guint j = i + 1; j < n_points; j++)
            {
              if (out_points[j].x != x)
                break;

              y = MIN (y, out_points[j].y);
            }

          x += alloc.x;

          cairo_move_to (cr, (guint)x + .5, alloc.height);
          cairo_line_to (cr, (guint)x + .5, y);
        }

      cairo_stroke (cr);
    }

  return ret;
}

static gboolean
sysprof_depth_visualizer_do_reload (gpointer data)
{
  SysprofDepthVisualizer *self = data;
  self->reload_source = 0;
  sysprof_depth_visualizer_reload (self);
  return G_SOURCE_REMOVE;
}

static void
sysprof_depth_visualizer_queue_reload (SysprofDepthVisualizer *self)
{
  g_assert (SYSPROF_IS_DEPTH_VISUALIZER (self));

  if (self->reload_source)
    g_source_remove (self->reload_source);

  self->reload_source = gdk_threads_add_idle (sysprof_depth_visualizer_do_reload, self);
}

static void
sysprof_depth_visualizer_size_allocate (GtkWidget     *widget,
                                        GtkAllocation *alloc)
{
  SysprofDepthVisualizer *self = (SysprofDepthVisualizer *)widget;

  GTK_WIDGET_CLASS (sysprof_depth_visualizer_parent_class)->size_allocate (widget, alloc);

  if (alloc->width != self->last_alloc.x ||
      alloc->height != self->last_alloc.height)
    {
      sysprof_depth_visualizer_queue_reload (SYSPROF_DEPTH_VISUALIZER (widget));
      self->last_alloc = *alloc;
    }
}

static void
sysprof_depth_visualizer_finalize (GObject *object)
{
  SysprofDepthVisualizer *self = (SysprofDepthVisualizer *)object;

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);

  if (self->reload_source)
    {
      g_source_remove (self->reload_source);
      self->reload_source = 0;
    }

  G_OBJECT_CLASS (sysprof_depth_visualizer_parent_class)->finalize (object);
}

static void
sysprof_depth_visualizer_class_init (SysprofDepthVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofVisualizerClass *row_class = SYSPROF_VISUALIZER_CLASS (klass);

  object_class->finalize = sysprof_depth_visualizer_finalize;

  widget_class->draw = sysprof_depth_visualizer_draw;
  widget_class->size_allocate = sysprof_depth_visualizer_size_allocate;

  row_class->set_reader = sysprof_depth_visualizer_set_reader;
}

static void
sysprof_depth_visualizer_init (SysprofDepthVisualizer *self)
{
}

SysprofVisualizer *
sysprof_depth_visualizer_new (SysprofDepthVisualizerMode  mode)
{
  SysprofDepthVisualizer *self;

  g_return_val_if_fail (mode == SYSPROF_DEPTH_VISUALIZER_COMBINED ||
                        mode == SYSPROF_DEPTH_VISUALIZER_KERNEL_ONLY ||
                        mode == SYSPROF_DEPTH_VISUALIZER_USER_ONLY,
                        NULL);

  self = g_object_new (SYSPROF_TYPE_DEPTH_VISUALIZER, NULL);
  self->mode = mode;

  return SYSPROF_VISUALIZER (g_steal_pointer (&self));
}
