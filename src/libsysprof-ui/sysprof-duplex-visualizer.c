/* sysprof-duplex-visualizer.c
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

#define G_LOG_DOMAIN "sysprof-duplex-visualizer"

#include "config.h"

#include "pointcache.h"
#include "sysprof-duplex-visualizer.h"

#define LABEL_HEIGHT_PX 10

struct _SysprofDuplexVisualizer
{
  SysprofVisualizer parent_instance;

  gint64 begin_time;
  gint64 duration;

  guint rx_counter;
  guint tx_counter;

  GdkRGBA rx_rgba;
  GdkRGBA tx_rgba;

  gchar *rx_label;
  gchar *tx_label;

  PointCache *cache;

  guint rx_rgba_set : 1;
  guint tx_rgba_set : 1;
  guint use_diff : 1;
};

typedef struct
{
  PointCache *cache;

  gint64      begin_time;
  gint64      duration;

  gint64      max_change;

  /* Last value to convert to rate of change */
  gint64      last_rx_val;
  gint64      last_tx_val;

  /* Counter IDs */
  guint       rx;
  guint       tx;

  /* Do we need to subtract previous value */
  guint       use_diff : 1;
} Collect;

G_DEFINE_TYPE (SysprofDuplexVisualizer, sysprof_duplex_visualizer, SYSPROF_TYPE_VISUALIZER)

static bool
collect_ranges_cb (const SysprofCaptureFrame *frame,
                   gpointer                   data)
{
  Collect *state = data;

  g_assert (frame != NULL);
  g_assert (state != NULL);
  g_assert (state->cache != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET)
    {
      const SysprofCaptureCounterSet *set = (gconstpointer)frame;

      for (guint i = 0; i < set->n_values; i++)
        {
          const SysprofCaptureCounterValues *values = &set->values[i];

          for (guint j = 0; j < G_N_ELEMENTS (values->ids); j++)
            {
              gint64 v64 = values->values[j].v64;
              guint id = values->ids[j];
              gint64 max_change = 0;

              if (id == 0)
                break;

              if (id == state->rx)
                {
                  if (state->last_rx_val != G_MININT64)
                    max_change = v64 - state->last_rx_val;
                  state->last_rx_val = v64;
                }
              else if (id == state->tx)
                {
                  if (state->last_tx_val != G_MININT64)
                    max_change = v64 - state->last_tx_val;
                  state->last_tx_val = v64;
                }
              else
                {
                  continue;
                }

              if (max_change > state->max_change)
                state->max_change = max_change;
            }
        }
    }

  return TRUE;
}

static bool
collect_values_cb (const SysprofCaptureFrame *frame,
                   gpointer                   data)
{
  Collect *state = data;

  g_assert (frame != NULL);
  g_assert (state != NULL);
  g_assert (state->cache != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET)
    {
      const SysprofCaptureCounterSet *set = (gconstpointer)frame;
      gdouble x = (frame->time - state->begin_time) / (gdouble)state->duration;

      for (guint i = 0; i < set->n_values; i++)
        {
          const SysprofCaptureCounterValues *values = &set->values[i];

          for (guint j = 0; j < G_N_ELEMENTS (values->ids); j++)
            {
              gint64 v64 = values->values[j].v64;
              guint id = values->ids[j];
              gint64 val = v64;
              gdouble y = 0.5;

              if (id == 0)
                break;

              if (id == state->rx)
                {
                  if (state->use_diff)
                    {
                      if (state->last_rx_val == G_MININT64)
                        val = 0;
                      else
                        val -= state->last_rx_val;
                    }

                  /* RX goes upward from half point */
                  if (state->max_change != 0)
                    y += (gdouble)val / (gdouble)state->max_change / 2.0;

                  state->last_rx_val = v64;
                }
              else if (id == state->tx)
                {
                  if (state->use_diff)
                    {
                      if (state->last_tx_val == G_MININT64)
                        val = 0;
                      else
                        val -= state->last_tx_val;
                    }

                  /* TX goes downward from half point */
                  if (state->max_change != 0)
                    y -= (gdouble)val / (gdouble)state->max_change / 2.0;

                  state->last_tx_val = v64;
                }
              else
                {
                  continue;
                }

              point_cache_add_point_to_set (state->cache, id, x, y);
            }
        }
    }

  return TRUE;
}

static void
sysprof_duplex_visualizer_worker (GTask        *task,
                                  gpointer      source_object,
                                  gpointer      task_data,
                                  GCancellable *cancellable)
{
  SysprofCaptureCursor *cursor = task_data;
  SysprofDuplexVisualizer *self = source_object;
  Collect state = {0};

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_DUPLEX_VISUALIZER (self));
  g_assert (cursor != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  state.cache = point_cache_new ();
  state.begin_time = self->begin_time;
  state.duration = self->duration;
  state.rx = g_atomic_int_get (&self->rx_counter);
  state.tx = g_atomic_int_get (&self->tx_counter);
  state.last_rx_val = G_MININT64;
  state.last_tx_val = G_MININT64;
  state.max_change = 0;
  state.use_diff = self->use_diff;

  point_cache_add_set (state.cache, state.rx);
  point_cache_add_set (state.cache, state.tx);

  sysprof_capture_cursor_foreach (cursor, collect_ranges_cb, &state);
  sysprof_capture_cursor_reset (cursor);

  /* Give just a bit of overhead */
  state.max_change *= 1.1;

  /* Reset for calculations */
  state.last_rx_val = G_MININT64;
  state.last_tx_val = G_MININT64;

  sysprof_capture_cursor_foreach (cursor, collect_values_cb, &state);

  g_task_return_pointer (task,
                         g_steal_pointer (&state.cache),
                         (GDestroyNotify) point_cache_unref);
}

static void
load_data_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDuplexVisualizer *self = (SysprofDuplexVisualizer *)object;
  g_autoptr(PointCache) pc = NULL;

  g_assert (SYSPROF_IS_DUPLEX_VISUALIZER (self));
  g_assert (G_IS_TASK (result));

  if ((pc = g_task_propagate_pointer (G_TASK (result), NULL)))
    {
      g_clear_pointer (&self->cache, point_cache_unref);
      self->cache = g_steal_pointer (&pc);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sysprof_duplex_visualizer_set_reader (SysprofVisualizer    *visualizer,
                                      SysprofCaptureReader *reader)
{
  SysprofDuplexVisualizer *self = (SysprofDuplexVisualizer *)visualizer;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  SysprofCaptureCondition *c;
  guint counters[2];

  g_assert (SYSPROF_IS_DUPLEX_VISUALIZER (self));
  g_assert (reader != NULL);

  self->begin_time = sysprof_capture_reader_get_start_time (reader);
  self->duration = sysprof_capture_reader_get_end_time (reader)
                 - sysprof_capture_reader_get_start_time (reader);

  counters[0] = self->rx_counter;
  counters[1] = self->tx_counter;

  cursor = sysprof_capture_cursor_new (reader);
  c = sysprof_capture_condition_new_where_counter_in (G_N_ELEMENTS (counters), counters);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&c));

  task = g_task_new (self, NULL, load_data_cb, NULL);
  g_task_set_source_tag (task, sysprof_duplex_visualizer_set_reader);
  g_task_set_task_data (task,
                        g_steal_pointer (&cursor),
                        (GDestroyNotify)sysprof_capture_cursor_unref);
  g_task_run_in_thread (task, sysprof_duplex_visualizer_worker);
}

static gboolean
sysprof_duplex_visualizer_draw (GtkWidget *widget,
                                cairo_t   *cr)
{
  static const gdouble dashes[] = { 1.0, 2.0 };
  SysprofDuplexVisualizer *self = (SysprofDuplexVisualizer *)widget;
  PangoFontDescription *font_desc;
  GtkStyleContext *style_context;
  PangoLayout *layout;
  GtkAllocation alloc;
  GdkRectangle clip;
  gboolean ret;
  GdkRGBA fg;
  guint mid;

  g_assert (SYSPROF_IS_DUPLEX_VISUALIZER (self));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);
  gdk_cairo_get_clip_rectangle (cr, &clip);

  mid = alloc.height / 2;

  ret = GTK_WIDGET_CLASS (sysprof_duplex_visualizer_parent_class)->draw (widget, cr);

  style_context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_color (style_context,
                               gtk_style_context_get_state (style_context),
                               &fg);
  fg.alpha *= 0.4;

  /* Draw our center line */
  cairo_save (cr);
  cairo_set_line_width (cr, 1.0);
  cairo_set_dash (cr, dashes, G_N_ELEMENTS (dashes), 0);
  cairo_move_to (cr, 0, mid);
  cairo_line_to (cr, alloc.width, mid);
  gdk_cairo_set_source_rgba (cr, &fg);
  cairo_stroke (cr);
  cairo_restore (cr);

  if (self->cache != NULL)
    {
      g_autofree SysprofVisualizerAbsolutePoint *points = NULL;
      const Point *fpoints;
      guint n_fpoints = 0;

      cairo_save (cr);
      cairo_set_line_width (cr, 1.0);
      if (self->rx_rgba_set)
        gdk_cairo_set_source_rgba (cr, &self->rx_rgba);

      fpoints = point_cache_get_points (self->cache, self->rx_counter, &n_fpoints);

      if (n_fpoints > 0)
        {
          GdkRGBA rgba = self->rx_rgba;
          gdouble last_x;
          gdouble last_y;
          guint p;

          points = g_realloc_n (points, n_fpoints, sizeof *points);

          sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (self),
                                               (const SysprofVisualizerRelativePoint *)fpoints,
                                               n_fpoints,
                                               points,
                                               n_fpoints);

          /* Skip past data that we won't see anyway */
          for (p = 0; p < n_fpoints; p++)
            {
              if (points[p].x >= clip.x)
                break;
            }

          if (p >= n_fpoints)
            return ret;

          /* But get at least one data point to anchor out of view */
          if (p > 0)
            p--;

          last_x = points[p].x;
          last_y = points[p].y;

          cairo_move_to (cr, last_x, mid);
          cairo_line_to (cr, last_x, last_y);

          for (guint i = p + 1; i < n_fpoints; i++)
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

              if (points[i].x > clip.x + clip.width)
                break;
            }

          cairo_line_to (cr, last_x, mid);
          cairo_close_path (cr);
          cairo_stroke_preserve (cr);
          rgba.alpha *= 0.5;
          gdk_cairo_set_source_rgba (cr, &rgba);
          cairo_fill (cr);
        }

      cairo_restore (cr);

      /* AND NOW Tx */

      cairo_save (cr);
      cairo_set_line_width (cr, 1.0);
      if (self->tx_rgba_set)
        gdk_cairo_set_source_rgba (cr, &self->tx_rgba);

      fpoints = point_cache_get_points (self->cache, self->tx_counter, &n_fpoints);

      if (n_fpoints > 0)
        {
          GdkRGBA rgba = self->tx_rgba;
          gdouble last_x;
          gdouble last_y;
          guint p;

          points = g_realloc_n (points, n_fpoints, sizeof *points);

          sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (self),
                                               (const SysprofVisualizerRelativePoint *)fpoints,
                                               n_fpoints,
                                               points,
                                               n_fpoints);

          /* Skip past data that we won't see anyway */
          for (p = 0; p < n_fpoints; p++)
            {
              if (points[p].x >= clip.x)
                break;
            }

          if (p >= n_fpoints)
            return ret;

          /* But get at least one data point to anchor out of view */
          if (p > 0)
            p--;

          last_x = points[p].x;
          last_y = points[p].y;

          cairo_move_to (cr, last_x, mid);
          cairo_line_to (cr, last_x, last_y);

          for (guint i = p + 1; i < n_fpoints; i++)
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

              if (points[i].x > clip.x + clip.width)
                break;
            }

          cairo_line_to (cr, last_x, mid);
          cairo_close_path (cr);
          cairo_stroke_preserve (cr);
          rgba.alpha *= 0.5;
          gdk_cairo_set_source_rgba (cr, &rgba);
          cairo_fill (cr);
        }

      cairo_restore (cr);
    }

  layout = gtk_widget_create_pango_layout (widget, "");

  font_desc = pango_font_description_new ();
  pango_font_description_set_family_static (font_desc, "Monospace");
  pango_font_description_set_absolute_size (font_desc, LABEL_HEIGHT_PX * PANGO_SCALE);
  pango_layout_set_font_description (layout, font_desc);

  gdk_cairo_set_source_rgba (cr, &fg);

  cairo_move_to (cr, 2, 2);
  if (self->rx_label != NULL)
    pango_layout_set_text (layout, self->rx_label, -1);
  else
    pango_layout_set_text (layout, "RX", 2);
  pango_cairo_show_layout (cr, layout);

  cairo_move_to (cr, 2, mid + 2);
  if (self->tx_label != NULL)
    pango_layout_set_text (layout, self->tx_label, -1);
  else
    pango_layout_set_text (layout, "TX", 2);
  pango_cairo_show_layout (cr, layout);

  pango_font_description_free (font_desc);
  g_object_unref (layout);

  return ret;
}

static void
sysprof_duplex_visualizer_finalize (GObject *object)
{
  SysprofDuplexVisualizer *self = (SysprofDuplexVisualizer *)object;

  g_clear_pointer (&self->cache, point_cache_unref);
  g_clear_pointer (&self->rx_label, g_free);
  g_clear_pointer (&self->tx_label, g_free);

  G_OBJECT_CLASS (sysprof_duplex_visualizer_parent_class)->finalize (object);
}

static void
sysprof_duplex_visualizer_class_init (SysprofDuplexVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofVisualizerClass *visualizer_class = SYSPROF_VISUALIZER_CLASS (klass);

  object_class->finalize = sysprof_duplex_visualizer_finalize;

  widget_class->draw = sysprof_duplex_visualizer_draw;

  visualizer_class->set_reader = sysprof_duplex_visualizer_set_reader;
}

static void
sysprof_duplex_visualizer_init (SysprofDuplexVisualizer *self)
{
  self->use_diff = TRUE;
}

GtkWidget *
sysprof_duplex_visualizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_DUPLEX_VISUALIZER, NULL);
}

void
sysprof_duplex_visualizer_set_counters (SysprofDuplexVisualizer *self,
                                        guint                    rx_counter,
                                        guint                    tx_counter)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_VISUALIZER (self));
  g_return_if_fail (rx_counter != 0);
  g_return_if_fail (tx_counter != 0);

  self->rx_counter = rx_counter;
  self->tx_counter = tx_counter;
}

void
sysprof_duplex_visualizer_set_colors (SysprofDuplexVisualizer *self,
                                      const GdkRGBA           *rx_rgba,
                                      const GdkRGBA           *tx_rgba)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_VISUALIZER (self));

  if (rx_rgba)
    self->rx_rgba = *rx_rgba;
  self->rx_rgba_set = !!rx_rgba;

  if (tx_rgba)
    self->tx_rgba = *tx_rgba;
  self->tx_rgba_set = !!tx_rgba;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

gboolean
sysprof_duplex_visualizer_get_use_diff (SysprofDuplexVisualizer *self)
{
  g_return_val_if_fail (SYSPROF_IS_DUPLEX_VISUALIZER (self), FALSE);

  return self->use_diff;
}

void
sysprof_duplex_visualizer_set_use_diff (SysprofDuplexVisualizer *self,
                                        gboolean                 use_diff)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_VISUALIZER (self));

  self->use_diff = !!use_diff;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

void
sysprof_duplex_visualizer_set_labels (SysprofDuplexVisualizer *self,
                                      const gchar             *rx_label,
                                      const gchar             *tx_label)
{
  g_return_if_fail (SYSPROF_IS_DUPLEX_VISUALIZER (self));

  if (g_strcmp0 (rx_label, self->rx_label) != 0)
    {
      g_free (self->rx_label);
      self->rx_label = g_strdup (rx_label);
    }

  if (g_strcmp0 (tx_label, self->tx_label) != 0)
    {
      g_free (self->tx_label);
      self->tx_label = g_strdup (tx_label);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
}
