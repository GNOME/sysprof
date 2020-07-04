/* sysprof-time-visualizer.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "sysprof-time-visualizer"

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sysprof.h>

#include "pointcache.h"
#include "sysprof-time-visualizer.h"

typedef struct
{
  /*
   * Our reader as assigned by the visualizer system.
   */
  SysprofCaptureReader *reader;

  /*
   * An array of LineInfo which contains information about the counters
   * we need to render.
   */
  GArray *lines;

  /*
   * This is our set of cached points to render. Once it is assigned here,
   * it is immutable (and therefore may be shared with worker processes
   * that are rendering the points).
   */
  PointCache *cache;

  /*
   * If we have a new counter discovered or the reader is set, we might
   * want to delay loading until we return to the main loop. This can
   * help us avoid doing duplicate work.
   */
  guint queued_load;
} SysprofTimeVisualizerPrivate;

typedef struct
{
  guint id;
  gdouble line_width;
  GdkRGBA rgba;
  guint use_default_style : 1;
  guint use_dash : 1;
} LineInfo;

typedef struct
{
  SysprofCaptureCursor *cursor;
  GArray *lines;
  PointCache *cache;
  gint64 begin_time;
  gint64 end_time;
} LoadData;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofTimeVisualizer, sysprof_time_visualizer, SYSPROF_TYPE_VISUALIZER)

static void        sysprof_time_visualizer_load_data_async  (SysprofTimeVisualizer  *self,
                                                             GCancellable           *cancellable,
                                                             GAsyncReadyCallback     callback,
                                                             gpointer                user_data);
static PointCache *sysprof_time_visualizer_load_data_finish (SysprofTimeVisualizer  *self,
                                                             GAsyncResult           *result,
                                                             GError                **error);

static gdouble dashes[] = { 1.0, 2.0 };

static void
load_data_free (gpointer data)
{
  LoadData *load = data;

  if (load != NULL)
    {
      g_clear_pointer (&load->lines, g_array_unref);
      g_clear_pointer (&load->cursor, sysprof_capture_cursor_unref);
      g_clear_pointer (&load->cache, point_cache_unref);
      g_slice_free (LoadData, load);
    }
}

static GArray *
copy_array (GArray *ar)
{
  GArray *ret;

  ret = g_array_sized_new (FALSE, FALSE, g_array_get_element_size (ar), ar->len);
  g_array_set_size (ret, ar->len);
  memcpy (ret->data, ar->data, ar->len * g_array_get_element_size (ret));

  return ret;
}

static gboolean
sysprof_time_visualizer_draw (GtkWidget *widget,
                              cairo_t   *cr)
{
  SysprofTimeVisualizer *self = (SysprofTimeVisualizer *)widget;
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);
  GtkStyleContext *style_context;
  GtkStateFlags flags;
  GtkAllocation alloc;
  GdkRectangle clip;
  GdkRGBA foreground;
  gboolean ret;

  g_assert (SYSPROF_IS_TIME_VISUALIZER (widget));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sysprof_time_visualizer_parent_class)->draw (widget, cr);

  if (priv->cache == NULL)
    return ret;

  if (!gdk_cairo_get_clip_rectangle (cr, &clip))
    return ret;

  style_context = gtk_widget_get_style_context (widget);
  flags = gtk_widget_get_state_flags (widget);
  gtk_style_context_get_color (style_context, flags, &foreground);

  gdk_cairo_set_source_rgba (cr, &foreground);

  for (guint line = 0; line < priv->lines->len; line++)
    {
      g_autofree SysprofVisualizerAbsolutePoint *points = NULL;
      const LineInfo *line_info = &g_array_index (priv->lines, LineInfo, line);
      const Point *fpoints;
      guint n_fpoints = 0;

      fpoints = point_cache_get_points (priv->cache, line_info->id, &n_fpoints);

      if (n_fpoints > 0)
        {
          guint last_x = G_MAXUINT;

          points = g_new0 (SysprofVisualizerAbsolutePoint, n_fpoints);

          sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (self),
                                               (const SysprofVisualizerRelativePoint *)fpoints,
                                               n_fpoints,
                                               points,
                                               n_fpoints);

          cairo_set_line_width (cr, 1.0);

          for (guint i = 0; i < n_fpoints; i++)
            {
              if ((guint)points[i].x != last_x)
                last_x = (guint)points[i].x;
              else
                continue;

              cairo_move_to (cr, (guint)points[i].x + .5, alloc.height / 3);
              cairo_line_to (cr, (guint)points[i].x + .5, alloc.height / 3 * 2);
            }

          if (line_info->use_dash)
            cairo_set_dash (cr, dashes, G_N_ELEMENTS (dashes), 0);

          cairo_stroke (cr);
        }
    }

  return ret;
}

static void
sysprof_time_visualizer_load_data_cb (GObject      *object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
  SysprofTimeVisualizer *self = (SysprofTimeVisualizer *)object;
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);
  g_autoptr(GError) error = NULL;
  g_autoptr(PointCache) cache = NULL;

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));

  cache = sysprof_time_visualizer_load_data_finish (self, result, &error);

  if (cache == NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  g_clear_pointer (&priv->cache, point_cache_unref);
  priv->cache = g_steal_pointer (&cache);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
sysprof_time_visualizer_do_reload (gpointer data)
{
  SysprofTimeVisualizer *self = data;
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));

  priv->queued_load = 0;
  if (priv->reader != NULL)
    sysprof_time_visualizer_load_data_async (self,
                                            NULL,
                                            sysprof_time_visualizer_load_data_cb,
                                            NULL);

  return G_SOURCE_REMOVE;
}

static void
sysprof_time_visualizer_queue_reload (SysprofTimeVisualizer *self)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));

  if (priv->queued_load == 0)
    priv->queued_load =
      gdk_threads_add_idle_full (G_PRIORITY_LOW,
                                 sysprof_time_visualizer_do_reload,
                                 self,
                                 NULL);
}

static void
sysprof_time_visualizer_set_reader (SysprofVisualizer    *row,
                                    SysprofCaptureReader *reader)
{
  SysprofTimeVisualizer *self = (SysprofTimeVisualizer *)row;
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));

  if (priv->reader != reader)
    {
      if (priv->reader != NULL)
        {
          sysprof_capture_reader_unref (priv->reader);
          priv->reader = NULL;
        }

      if (reader != NULL)
        priv->reader = sysprof_capture_reader_ref (reader);

      sysprof_time_visualizer_queue_reload (self);
    }
}

static void
sysprof_time_visualizer_finalize (GObject *object)
{
  SysprofTimeVisualizer *self = (SysprofTimeVisualizer *)object;
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_clear_pointer (&priv->lines, g_array_unref);
  g_clear_pointer (&priv->cache, point_cache_unref);
  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);

  if (priv->queued_load != 0)
    {
      g_source_remove (priv->queued_load);
      priv->queued_load = 0;
    }

  G_OBJECT_CLASS (sysprof_time_visualizer_parent_class)->finalize (object);
}

static void
sysprof_time_visualizer_class_init (SysprofTimeVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofVisualizerClass *visualizer_class = SYSPROF_VISUALIZER_CLASS (klass);

  object_class->finalize = sysprof_time_visualizer_finalize;

  widget_class->draw = sysprof_time_visualizer_draw;

  visualizer_class->set_reader = sysprof_time_visualizer_set_reader;
}

static void
sysprof_time_visualizer_init (SysprofTimeVisualizer *self)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  priv->lines = g_array_new (FALSE, FALSE, sizeof (LineInfo));
}

void
sysprof_time_visualizer_add_counter (SysprofTimeVisualizer *self,
                                     guint                     counter_id,
                                     const GdkRGBA            *color)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);
  LineInfo line_info = {0};

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));
  g_assert (priv->lines != NULL);

  line_info.id = counter_id;
  line_info.line_width = 1.0;

  if (color != NULL)
    {
      line_info.rgba = *color;
      line_info.use_default_style = FALSE;
    }
  else
    {
      line_info.use_default_style = TRUE;
    }

  g_array_append_val (priv->lines, line_info);

  if (SYSPROF_TIME_VISUALIZER_GET_CLASS (self)->counter_added)
    SYSPROF_TIME_VISUALIZER_GET_CLASS (self)->counter_added (self, counter_id);

  sysprof_time_visualizer_queue_reload (self);
}

void
sysprof_time_visualizer_clear (SysprofTimeVisualizer *self)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_TIME_VISUALIZER (self));

  if (priv->lines->len > 0)
    g_array_remove_range (priv->lines, 0, priv->lines->len);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static inline gboolean
contains_id (GArray *ar,
             guint   id)
{
  for (guint i = 0; i < ar->len; i++)
    {
      const LineInfo *info = &g_array_index (ar, LineInfo, i);

      if (info->id == id)
        return TRUE;
    }

  return FALSE;
}

static inline gdouble
calc_x (gint64 lower,
        gint64 upper,
        gint64 value)
{
  return (gdouble)(value - lower) / (gdouble)(upper - lower);
}

static bool
sysprof_time_visualizer_load_data_frame_cb (const SysprofCaptureFrame *frame,
                                            gpointer                   user_data)
{
  LoadData *load = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET ||
            frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF);
  g_assert (load != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET)
    {
      const SysprofCaptureCounterSet *set = (SysprofCaptureCounterSet *)frame;
      gdouble x = calc_x (load->begin_time, load->end_time, frame->time);

      for (guint i = 0; i < set->n_values; i++)
        {
          const SysprofCaptureCounterValues *group = &set->values[i];

          for (guint j = 0; j < G_N_ELEMENTS (group->ids); j++)
            {
              guint counter_id = group->ids[j];

              if (counter_id != 0 && contains_id (load->lines, counter_id))
                point_cache_add_point_to_set (load->cache, counter_id, x, 0);
            }
        }
    }

  return TRUE;
}

static void
sysprof_time_visualizer_load_data_worker (GTask        *task,
                                          gpointer      source_object,
                                          gpointer      task_data,
                                          GCancellable *cancellable)
{
  LoadData *load = task_data;
  g_autoptr(GArray) counter_ids = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_TIME_VISUALIZER (source_object));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  counter_ids = g_array_new (FALSE, FALSE, sizeof (guint));

  for (guint i = 0; i < load->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (load->lines, LineInfo, i);
      g_array_append_val (counter_ids, line_info->id);
    }

  sysprof_capture_cursor_add_condition (load->cursor,
                                        sysprof_capture_condition_new_where_counter_in (counter_ids->len,
                                                                                        (guint *)(gpointer)counter_ids->data));
  sysprof_capture_cursor_foreach (load->cursor, sysprof_time_visualizer_load_data_frame_cb, load);
  g_task_return_pointer (task, g_steal_pointer (&load->cache), (GDestroyNotify)point_cache_unref);
}

static void
sysprof_time_visualizer_load_data_async (SysprofTimeVisualizer *self,
                                         GCancellable          *cancellable,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);
  g_autoptr(GTask) task = NULL;
  LoadData *load;

  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_priority (task, G_PRIORITY_LOW);
  g_task_set_source_tag (task, sysprof_time_visualizer_load_data_async);

  if (priv->reader == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "No data loaded");
      return;
    }

  load = g_slice_new0 (LoadData);
  load->cache = point_cache_new ();
  load->begin_time = sysprof_capture_reader_get_start_time (priv->reader);
  load->end_time = sysprof_capture_reader_get_end_time (priv->reader);
  load->cursor = sysprof_capture_cursor_new (priv->reader);
  load->lines = copy_array (priv->lines);

  for (guint i = 0; i < load->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (load->lines, LineInfo, i);

      point_cache_add_set (load->cache, line_info->id);
    }

  g_task_set_task_data  (task, load, load_data_free);
  g_task_run_in_thread (task, sysprof_time_visualizer_load_data_worker);
}

static PointCache *
sysprof_time_visualizer_load_data_finish (SysprofTimeVisualizer  *self,
                                          GAsyncResult           *result,
                                          GError                **error)
{
  g_assert (SYSPROF_IS_TIME_VISUALIZER (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
sysprof_time_visualizer_set_line_width (SysprofTimeVisualizer *self,
                                        guint                  counter_id,
                                        gdouble                width)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_TIME_VISUALIZER (self));

  for (guint i = 0; i < priv->lines->len; i++)
    {
      LineInfo *info = &g_array_index (priv->lines, LineInfo, i);

      if (info->id == counter_id)
        {
          info->line_width = width;
          sysprof_time_visualizer_queue_reload (self);
          break;
        }
    }
}

void
sysprof_time_visualizer_set_dash (SysprofTimeVisualizer *self,
                                  guint                  counter_id,
                                  gboolean               use_dash)
{
  SysprofTimeVisualizerPrivate *priv = sysprof_time_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_TIME_VISUALIZER (self));

  for (guint i = 0; i < priv->lines->len; i++)
    {
      LineInfo *info = &g_array_index (priv->lines, LineInfo, i);

      if (info->id == counter_id)
        {
          info->use_dash = !!use_dash;
          sysprof_time_visualizer_queue_reload (self);
          break;
        }
    }
}
