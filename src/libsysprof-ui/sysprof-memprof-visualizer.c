/* sysprof-memprof-visualizer.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#define G_LOG_DOMAIN "sysprof-memprof-visualizer"

#include <glib/gi18n.h>
#include <math.h>
#include <stddef.h>

#include "rax.h"

#include "sysprof-memprof-visualizer.h"

typedef struct
{
  cairo_surface_t      *surface;
  SysprofCaptureReader *reader;
  rax                  *rax;
  GtkAllocation         alloc;
  gint64                begin_time;
  gint64                duration;
  gint64                total_alloc;
  gint64                max_alloc;
  GdkRGBA               fg;
  GdkRGBA               fg2;
  guint                 scale;
} DrawContext;

struct _SysprofMemprofVisualizer
{
  SysprofVisualizer     parent_instance;

  SysprofCaptureReader *reader;
  GCancellable         *cancellable;

  cairo_surface_t      *surface;
  gint                  surface_w;
  gint                  surface_h;

  guint                 queued_draw;

  gint64                begin_time;
  gint64                duration;

  gint64                cached_total_alloc;
  gint64                cached_max_alloc;

  guint                 mode : 1;
};

enum {
  MODE_ALLOCS,
  MODE_TOTAL,
};

G_DEFINE_TYPE (SysprofMemprofVisualizer, sysprof_memprof_visualizer, SYSPROF_TYPE_VISUALIZER)

static void
draw_context_free (DrawContext *draw)
{
  g_clear_pointer (&draw->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&draw->surface, cairo_surface_destroy);
  g_clear_pointer (&draw->rax, raxFree);
  g_slice_free (DrawContext, draw);
}

static void
sysprof_memprof_visualizer_set_reader (SysprofVisualizer    *visualizer,
                                       SysprofCaptureReader *reader)
{
  SysprofMemprofVisualizer *self = (SysprofMemprofVisualizer *)visualizer;

  g_assert (SYSPROF_IS_MEMPROF_VISUALIZER (self));

  if (reader == self->reader)
    return;

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);

  self->reader = sysprof_capture_reader_ref (reader);
  self->begin_time = sysprof_capture_reader_get_start_time (reader);
  self->duration = sysprof_capture_reader_get_end_time (reader)
                 - sysprof_capture_reader_get_start_time (reader);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

SysprofVisualizer *
sysprof_memprof_visualizer_new (gboolean total_allocs)
{
  SysprofMemprofVisualizer *self;

  self = g_object_new (SYSPROF_TYPE_MEMPROF_VISUALIZER,
                       "title", total_allocs ? _("Memory Used") : _("Memory Allocations"),
                       "height-request", 35,
                       "visible", TRUE,
                       NULL);

  if (total_allocs)
    self->mode = MODE_TOTAL;
  else
    self->mode = MODE_ALLOCS;

  return SYSPROF_VISUALIZER (self);
}

static guint64
get_max_alloc (SysprofCaptureReader *reader)
{
  SysprofCaptureFrameType type;
  gint64 ret = 0;

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      const SysprofCaptureAllocation *ev;

      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          if (!(ev = sysprof_capture_reader_read_allocation (reader)))
            break;

          if (ev->alloc_size > ret)
            ret = ev->alloc_size;
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
          continue;
        }
    }

  sysprof_capture_reader_reset (reader);

  return ret;
}

static guint64
get_total_alloc (SysprofCaptureReader *reader)
{
  SysprofCaptureFrameType type;
  guint64 total = 0;
  guint64 max = 0;
  rax *r;

  r = raxNew ();

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      const SysprofCaptureAllocation *ev;

      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          if (!(ev = sysprof_capture_reader_read_allocation (reader)))
            break;

          if (ev->alloc_size > 0)
            {
              raxInsert (r,
                         (guint8 *)&ev->alloc_addr,
                         sizeof ev->alloc_addr,
                         GSIZE_TO_POINTER (ev->alloc_size),
                         NULL);

              total += ev->alloc_size;

              if (total > max)
                max = total;
            }
          else
            {
              gpointer res = raxFind (r, (guint8 *)&ev->alloc_addr, sizeof ev->alloc_addr);

              if (res != raxNotFound)
                {
                  total -= GPOINTER_TO_SIZE (res);
                  raxRemove (r,
                             (guint8 *)&ev->alloc_addr,
                             sizeof ev->alloc_addr,
                             NULL);
                }
            }
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
          continue;
        }
    }

  sysprof_capture_reader_reset (reader);
  raxFree (r);

  return max;
}

static void
draw_total_worker (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  SysprofCaptureFrameType type;
  DrawContext *draw = task_data;
  gint64 total = 0;
  cairo_t *cr;
  rax *r;
  gint x = 0;

  g_assert (G_IS_TASK (task));
  g_assert (draw != NULL);
  g_assert (draw->surface != NULL);
  g_assert (draw->reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (draw->total_alloc == 0)
    draw->total_alloc = get_total_alloc (draw->reader);

  r = raxNew ();

  /* To avoid sorting, this code assums that all allocation information
   * is sorted and in order. Generally this is the case, but a crafted
   * syscap file could break it on purpose if they tried.
   */

  cr = cairo_create (draw->surface);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_source_rgb (cr, 0, 0, 0);

  while (sysprof_capture_reader_peek_type (draw->reader, &type))
    {
      const SysprofCaptureAllocation *ev;
      gint y;

      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          if (!(ev = sysprof_capture_reader_read_allocation (draw->reader)))
            break;

          if (ev->alloc_size > 0)
            {
              raxInsert (r,
                         (guint8 *)&ev->alloc_addr,
                         sizeof ev->alloc_addr,
                         GSIZE_TO_POINTER (ev->alloc_size),
                         NULL);

              total += ev->alloc_size;
            }
          else
            {
              gpointer res = raxFind (r, (guint8 *)&ev->alloc_addr, sizeof ev->alloc_addr);

              if (res != raxNotFound)
                {
                  total -= GPOINTER_TO_SIZE (res);
                  raxRemove (r,
                             (guint8 *)&ev->alloc_addr,
                             sizeof ev->alloc_addr,
                             NULL);
                }
            }
        }
      else
        {
          if (!sysprof_capture_reader_skip (draw->reader))
            break;
          continue;
        }

      x = (ev->frame.time - draw->begin_time) / (gdouble)draw->duration * draw->alloc.width;
      y = draw->alloc.height - ((gdouble)total / (gdouble)draw->total_alloc * (gdouble)draw->alloc.height);

      cairo_rectangle (cr, x, y, 1, 1);
      cairo_fill (cr);
    }

  cairo_destroy (cr);

  g_task_return_boolean (task, TRUE);

  raxFree (r);
}

static void
draw_alloc_worker (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  static const gdouble dashes[] = { 1.0, 2.0 };
  DrawContext *draw = task_data;
  SysprofCaptureFrameType type;
  GdkRGBA *last;
  GdkRGBA mid;
  cairo_t *cr;
  guint counter = 0;
  gint midpt;
  gdouble log_max;

  g_assert (G_IS_TASK (task));
  g_assert (draw != NULL);
  g_assert (draw->surface != NULL);
  g_assert (draw->reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (draw->max_alloc == 0)
    draw->max_alloc = get_max_alloc (draw->reader);

  log_max = log10 (draw->max_alloc);
  midpt = draw->alloc.height / 2;

  cr = cairo_create (draw->surface);

  /* Draw mid-point line */
  mid = draw->fg;
  mid.alpha *= 0.4;
  cairo_set_line_width (cr, 1.0);
  gdk_cairo_set_source_rgba (cr, &mid);
  cairo_move_to (cr, 0, midpt);
  cairo_line_to (cr, draw->alloc.width, midpt);
  cairo_set_dash (cr, dashes, G_N_ELEMENTS (dashes), 0);
  cairo_stroke (cr);

  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  gdk_cairo_set_source_rgba (cr, &draw->fg);
  last = &draw->fg;

  /* Now draw data points */
  while (sysprof_capture_reader_peek_type (draw->reader, &type))
    {
      const SysprofCaptureAllocation *ev;
      gint64 size;
      gdouble l;
      gint x;
      gint y;

      /* Cancellation check every 1000 frames */
      if G_UNLIKELY (++counter == 1000)
        {
          if (g_task_return_error_if_cancelled (task))
            {
              cairo_destroy (cr);
              return;
            }

          counter = 0;
        }

      /* We only care about memory frames here */
      if (type != SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          if (!sysprof_capture_reader_skip (draw->reader))
            break;
          continue;
        }

      if (!(ev = sysprof_capture_reader_read_allocation (draw->reader)))
        break;

      if (ev->alloc_size > 0)
        {
          size = ev->alloc_size;
          raxInsert (draw->rax, (guint8 *)&ev->alloc_addr, sizeof ev->alloc_addr, GSIZE_TO_POINTER (size), NULL);

          if (last != &draw->fg)
            {
              gdk_cairo_set_source_rgba (cr, &draw->fg);
              last = &draw->fg;
            }
        }
      else
        {
          size = GPOINTER_TO_SIZE (raxFind (draw->rax, (guint8 *)&ev->alloc_addr, sizeof ev->alloc_addr));
          if (size)
            raxRemove (draw->rax, (guint8 *)&ev->alloc_addr, sizeof ev->alloc_addr, NULL);

          if (last != &draw->fg2)
            {
              gdk_cairo_set_source_rgba (cr, &draw->fg2);
              last = &draw->fg2;
            }
        }

      l = log10 (size);

      x = (ev->frame.time - draw->begin_time) / (gdouble)draw->duration * draw->alloc.width;

      if (ev->alloc_size > 0)
        y = midpt - ((l / log_max) * midpt);
      else
        y = midpt + ((l / log_max) * midpt);

      /* Fill immediately instead of batching draws so that
       * we don't take a lot of memory to hold on to the
       * path while drawing.
       */
      cairo_rectangle (cr, x, y, 1, 1);
      cairo_fill (cr);
    }

  cairo_destroy (cr);

  g_task_return_boolean (task, TRUE);
}

static void
draw_finished (GObject      *object,
               GAsyncResult *result,
               gpointer      user_data)
{
  g_autoptr(SysprofMemprofVisualizer) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (object == NULL);
  g_assert (G_IS_TASK (result));
  g_assert (SYSPROF_IS_MEMPROF_VISUALIZER (self));

  if (g_task_propagate_boolean (G_TASK (result), &error))
    {
      DrawContext *draw = g_task_get_task_data (G_TASK (result));

      g_clear_pointer (&self->surface, cairo_surface_destroy);

      self->surface = g_steal_pointer (&draw->surface);
      self->surface_w = draw->alloc.width;
      self->surface_h = draw->alloc.height;
      self->cached_max_alloc = draw->max_alloc;
      self->cached_total_alloc = draw->total_alloc;

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static gboolean
sysprof_memprof_visualizer_begin_draw (SysprofMemprofVisualizer *self)
{
  g_autoptr(GTask) task = NULL;
  GtkAllocation alloc;
  DrawContext *draw;

  g_assert (SYSPROF_IS_MEMPROF_VISUALIZER (self));

  self->queued_draw = 0;

  /* Make sure we even need to draw */
  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);
  if (self->reader == NULL ||
      !gtk_widget_get_visible (GTK_WIDGET (self)) ||
      !gtk_widget_get_mapped (GTK_WIDGET (self)) ||
      alloc.width == 0 || alloc.height == 0)
    return G_SOURCE_REMOVE;

  /* Some GPUs (Intel) cannot deal with graphics textures larger than
   * 8000x8000. So here we are going to cheat a bit and just use that as our
   * max, and scale when drawing. The biggest issue here is that long term we
   * need a tiling solution that lets us render lots of tiles and then draw
   * them as necessary.
   */
  if (alloc.width > 8000)
    alloc.width = 8000;

  draw = g_slice_new0 (DrawContext);
  draw->rax = raxNew ();
  draw->alloc.width = alloc.width;
  draw->alloc.height = alloc.height;
  draw->reader = sysprof_capture_reader_copy (self->reader);
  draw->begin_time = self->begin_time;
  draw->duration = self->duration;
  draw->scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
  draw->max_alloc = self->cached_max_alloc;
  draw->total_alloc = self->cached_total_alloc;

  gdk_rgba_parse (&draw->fg, "rgba(246,97,81,1)");
  gdk_rgba_parse (&draw->fg2, "rgba(245,194,17,1)");

  draw->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                              alloc.width * draw->scale,
                                              alloc.height * draw->scale);
  cairo_surface_set_device_scale (draw->surface, draw->scale, draw->scale);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  task = g_task_new (NULL, self->cancellable, draw_finished, g_object_ref (self));
  g_task_set_source_tag (task, sysprof_memprof_visualizer_begin_draw);
  g_task_set_task_data (task, g_steal_pointer (&draw), (GDestroyNotify)draw_context_free);

  if (self->mode == MODE_ALLOCS)
    g_task_run_in_thread (task, draw_alloc_worker);
  else
    g_task_run_in_thread (task, draw_total_worker);

  return G_SOURCE_REMOVE;
}

static void
sysprof_memprof_visualizer_queue_redraw (SysprofMemprofVisualizer *self)
{
  g_assert (SYSPROF_IS_MEMPROF_VISUALIZER (self));

  if (self->queued_draw == 0)
    self->queued_draw = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                                         (GSourceFunc) sysprof_memprof_visualizer_begin_draw,
                                         g_object_ref (self),
                                         g_object_unref);
}

static void
sysprof_memprof_visualizer_size_allocate (GtkWidget     *widget,
                                          GtkAllocation *alloc)
{
  SysprofMemprofVisualizer *self = (SysprofMemprofVisualizer *)widget;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (alloc != NULL);

  GTK_WIDGET_CLASS (sysprof_memprof_visualizer_parent_class)->size_allocate (widget, alloc);

  sysprof_memprof_visualizer_queue_redraw (self);
}

static void
sysprof_memprof_visualizer_destroy (GtkWidget *widget)
{
  SysprofMemprofVisualizer *self = (SysprofMemprofVisualizer *)widget;

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&self->surface, cairo_surface_destroy);
  g_clear_handle_id (&self->queued_draw, g_source_remove);

  GTK_WIDGET_CLASS (sysprof_memprof_visualizer_parent_class)->destroy (widget);
}

static gboolean
sysprof_memprof_visualizer_draw (GtkWidget *widget,
                                 cairo_t   *cr)
{
  SysprofMemprofVisualizer *self = (SysprofMemprofVisualizer *)widget;
  gboolean ret;

  g_assert (SYSPROF_IS_MEMPROF_VISUALIZER (self));
  g_assert (cr != NULL);

  ret = GTK_WIDGET_CLASS (sysprof_memprof_visualizer_parent_class)->draw (widget, cr);

  if (self->surface != NULL)
    {
      GtkAllocation alloc;

      gtk_widget_get_allocation (widget, &alloc);

      cairo_save (cr);
      cairo_rectangle (cr, 0, 0, alloc.width, alloc.height);

      /* We might be drawing an updated image in the background, and this
       * will take our current surface (which is the wrong size) and draw
       * it stretched to fit the allocation. That gives us *something* that
       * represents the end result even if it is a bit blurry in the mean
       * time. Allocators take a while to render anyway.
       */
      if (self->surface_w != alloc.width || self->surface_h != alloc.height)
        {
          cairo_scale (cr,
                       (gdouble)alloc.width / (gdouble)self->surface_w,
                       (gdouble)alloc.height / (gdouble)self->surface_h);
        }

      cairo_set_source_surface (cr, self->surface, 0, 0);
      cairo_paint (cr);
      cairo_restore (cr);
    }

  return ret;
}

static void
sysprof_memprof_visualizer_class_init (SysprofMemprofVisualizerClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofVisualizerClass *visualizer_class = SYSPROF_VISUALIZER_CLASS (klass);

  widget_class->destroy = sysprof_memprof_visualizer_destroy;
  widget_class->draw = sysprof_memprof_visualizer_draw;
  widget_class->size_allocate = sysprof_memprof_visualizer_size_allocate;

  visualizer_class->set_reader = sysprof_memprof_visualizer_set_reader;
}

static void
sysprof_memprof_visualizer_init (SysprofMemprofVisualizer *self)
{
}
