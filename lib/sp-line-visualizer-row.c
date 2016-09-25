/* sp-line-visualizer-row.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#define G_LOG_DOMAIN "sp-line-visualizer-row"

#include <stdlib.h>

#include "sp-line-visualizer-row.h"

typedef struct
{
  SpCaptureReader *reader;

  GHashTable      *counters;
  GtkLabel        *label;

  cairo_surface_t *surface;
  guint            draw_sequence;
} SpLineVisualizerRowPrivate;

typedef struct
{
  gint64 begin_time;
  gint64 end_time;
  gint width;
  gint height;
  guint sequence;
} DrawRequest;

G_DEFINE_TYPE_WITH_PRIVATE (SpLineVisualizerRow, sp_line_visualizer_row, SP_TYPE_VISUALIZER_ROW)

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sp_line_visualizer_row_draw_worker (GTask        *task,
                                    gpointer      source_object,
                                    gpointer      task_data,
                                    GCancellable *cancellable)
{
  SpLineVisualizerRow *self = source_object;
  DrawRequest *draw = task_data;
  cairo_surface_t *surface = NULL;
  cairo_t *cr = NULL;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (task));
  g_assert (draw != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /*
   * Create our image surface. We do this in the local thread and use
   * an image surface so that we can work around the lack of thread
   * safety in cairo and pixman. They are mostly thread unaware though,
   * so as long as we are really careful, we are okay.
   *
   * We draw using ARGB32 because the GtkListBoxRow will be doing it's
   * own pixel caching. This means we can concentrate on the line data
   * and ignore having to deal with backgrounds and such.
   */
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, draw->width, draw->height);

  if (surface == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to create image surface of size %dx%d",
                               draw->width, draw->height);
      goto cleanup;
    }

  cr = cairo_create (surface);

  if (cr == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to create cairo context");
      goto cleanup;
    }

  g_task_return_pointer (task, g_steal_pointer (&surface), (GDestroyNotify)cairo_surface_destroy);

cleanup:
  if (surface != NULL)
    cairo_surface_destroy (surface);

  if (cr != NULL)
    cairo_destroy (cr);
}

static void
complete_draw (GObject      *object,
               GAsyncResult *result,
               gpointer      user_data)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  DrawRequest *draw;
  GTask *task = (GTask *)result;
  cairo_surface_t *surface;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (task));

  draw = g_task_get_task_data (task);
  surface = g_task_propagate_pointer (task, NULL);

  if (surface != NULL && draw->sequence == priv->draw_sequence)
    {
      g_clear_pointer (&priv->surface, cairo_surface_destroy);
      priv->surface = surface;
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sp_line_visualizer_row_begin_draw (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  g_autoptr(GTask) task = NULL;
  DrawRequest *draw;
  gint64 begin_time;
  gint64 end_time;
  GtkAllocation alloc;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  sp_visualizer_row_get_time_range (SP_VISUALIZER_ROW (self), &begin_time, &end_time);
  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  draw = g_new (DrawRequest, 1);
  draw->begin_time = begin_time;
  draw->end_time = end_time;
  draw->width = alloc.width;
  draw->height = alloc.height;
  draw->sequence = ++priv->draw_sequence;

  task = g_task_new (self, NULL, complete_draw, NULL);
  g_task_set_source_tag (task, sp_line_visualizer_row_begin_draw);
  g_task_set_task_data (task, draw, g_free);
  g_task_run_in_thread (task, sp_line_visualizer_row_draw_worker);
}

static gboolean
sp_line_visualizer_row_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  GtkAllocation alloc;
  gboolean ret;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (widget));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->draw (widget, cr);

  if (ret == GDK_EVENT_PROPAGATE && priv->surface != NULL)
    {
      gint width;
      gint height;

      width = cairo_image_surface_get_width (priv->surface);
      height = cairo_image_surface_get_height (priv->surface);

      /*
       * We must have another threaded draw queued, so to give the impression
       * that we are quickly growing with the widget, we will scale the draw
       * and then paint our wrongly sized surface (which will likely be a bit
       * blurred, but that's okay).
       */
      if (width != alloc.width || height != alloc.height)
        {

          return ret;
        }

      /*
       * This is our ideal path, where we have a 1-to-1 match of our backing
       * surface matching the widgets current allocation.
       */
      cairo_rectangle (cr, 0, 0, alloc.width, alloc.height);
      cairo_set_source_surface (cr, priv->surface, 0, 0);
      cairo_fill (cr);
    }

  return ret;
}

static void
sp_line_visualizer_row_set_reader (SpVisualizerRow *row,
                                   SpCaptureReader *reader)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)row;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  if (priv->reader != reader)
    {
      if (priv->reader != NULL)
        {
          sp_capture_reader_unref (priv->reader);
          priv->reader = NULL;
        }

      if (reader != NULL)
        priv->reader = sp_capture_reader_ref (reader);

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sp_line_visualizer_row_real_prepare (SpLineVisualizerRow *self,
                                     cairo_t             *cr,
                                     guint                counter_id)
{
  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (cr != NULL);

  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.4);
}

static void
sp_line_visualizer_row_finalize (GObject *object)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->counters, g_hash_table_unref);
  g_clear_pointer (&priv->reader, sp_capture_reader_unref);

  G_OBJECT_CLASS (sp_line_visualizer_row_parent_class)->finalize (object);
}

static void
sp_line_visualizer_row_size_allocate (GtkWidget     *widget,
                                      GtkAllocation *alloc)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (alloc != NULL);

  GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->size_allocate (widget, alloc);

  sp_line_visualizer_row_begin_draw (self);
}

static void
sp_line_visualizer_row_destroy (GtkWidget *widget)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  g_clear_pointer (&priv->surface, cairo_surface_destroy);

  GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->destroy (widget);
}

static void
sp_line_visualizer_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpLineVisualizerRow *self = SP_LINE_VISUALIZER_ROW (object);
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_get_property (G_OBJECT (priv->label), "label", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_line_visualizer_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SpLineVisualizerRow *self = SP_LINE_VISUALIZER_ROW (object);
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_set_property (G_OBJECT (priv->label), "label", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_line_visualizer_row_class_init (SpLineVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SpVisualizerRowClass *visualizer_class = SP_VISUALIZER_ROW_CLASS (klass);

  klass->prepare = sp_line_visualizer_row_real_prepare;

  object_class->finalize = sp_line_visualizer_row_finalize;
  object_class->get_property = sp_line_visualizer_row_get_property;
  object_class->set_property = sp_line_visualizer_row_set_property;

  widget_class->draw = sp_line_visualizer_row_draw;
  widget_class->destroy = sp_line_visualizer_row_destroy;
  widget_class->size_allocate = sp_line_visualizer_row_size_allocate;

  visualizer_class->set_reader = sp_line_visualizer_row_set_reader;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_line_visualizer_row_init (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  PangoAttrList *attrs = pango_attr_list_new ();

  priv->counters = g_hash_table_new (NULL, NULL);

  pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL * PANGO_SCALE_SMALL));

  priv->label = g_object_new (GTK_TYPE_LABEL,
                              "attributes", attrs,
                              "visible", TRUE,
                              "xalign", 0.0f,
                              "yalign", 0.0f,
                              NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (priv->label));

  pango_attr_list_unref (attrs);
}

void
sp_line_visualizer_row_add_counter (SpLineVisualizerRow *self,
                                    guint                counter_id)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (priv->counters != NULL);

  g_hash_table_insert (priv->counters, GSIZE_TO_POINTER (counter_id), NULL);

  if (SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added)
    SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added (self, counter_id);

  /*
   * We use queue_resize so that a size_allocate() is called
   * to kick off the new threaded draw of the widget. Otherwise,
   * we'd have to do our own queuing, so this simplifies things
   * a small bit.
   */
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
sp_line_visualizer_row_clear (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_LINE_VISUALIZER_ROW (self));

  g_hash_table_remove_all (priv->counters);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}
