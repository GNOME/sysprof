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
#include <string.h>

#include "pointcache.h"
#include "sp-capture-condition.h"
#include "sp-capture-cursor.h"
#include "sp-line-visualizer-row.h"

typedef struct
{
  /*
   * Our reader as assigned by the visualizer system.
   */
  SpCaptureReader *reader;

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
   * This is our offscreen surface that is already rendered. We use it
   * to render to the front buffer (during our ::draw vfunc).
   */
  cairo_surface_t *surface;

  /*
   * We track the draw sequence so that we only "consume" the newest
   * draw operation after a threaded render completes.
   */
  guint draw_sequence;

  /*
   * Child widget to display the label in the upper corner.
   */
  GtkLabel *label;

  /*
   * Range of the scale for lower and upper.
   */
  gdouble y_lower;
  gdouble y_upper;

  /*
   * If we have a new counter discovered or the reader is set, we might
   * want to delay loading until we return to the main loop. This can
   * help us avoid doing duplicate work.
   */
  guint queued_load;
} SpLineVisualizerRowPrivate;

typedef struct
{
  guint id;
  gdouble line_width;
  GdkRGBA background;
  GdkRGBA foreground;
  guint use_default_style : 1;
} LineInfo;

typedef struct
{
  PointCache *cache;
  GArray *lines;
  GdkRGBA color;
  gint scale_factor;
  gint width;
  gint height;
} RenderData;

typedef struct
{
  SpCaptureCursor *cursor;
  GArray *lines;
  PointCache *cache;
  gint64 begin_time;
  gint64 end_time;
  gdouble y_lower;
  gdouble y_upper;
} LoadData;

G_DEFINE_TYPE_WITH_PRIVATE (SpLineVisualizerRow, sp_line_visualizer_row, SP_TYPE_VISUALIZER_ROW)

static void            sp_line_visualizer_row_load_data_async   (SpLineVisualizerRow  *self,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
static PointCache      *sp_line_visualizer_row_load_data_finish (SpLineVisualizerRow  *self,
                                                                 GAsyncResult         *result,
                                                                 GError              **error);
static void            sp_line_visualizer_row_render_async      (SpLineVisualizerRow  *self,
                                                                 PointCache           *cache,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
static cairo_surface_t *sp_line_visualizer_row_render_finish    (SpLineVisualizerRow  *self,
                                                                 GAsyncResult         *result,
                                                                 GError              **error);

enum {
  PROP_0,
  PROP_TITLE,
  PROP_Y_LOWER,
  PROP_Y_UPPER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
load_data_free (gpointer data)
{
  LoadData *load = data;

  if (load != NULL)
    {
      g_clear_pointer (&load->lines, g_array_unref);
      g_clear_object (&load->cursor);
      g_clear_pointer (&load->cache, point_cache_unref);
      g_slice_free (LoadData, load);
    }
}

static void
render_data_free (gpointer data)
{
  RenderData *render = data;

  if (render != NULL)
    {
      g_clear_pointer (&render->cache, point_cache_unref);
      g_clear_pointer (&render->lines, g_array_unref);
      g_slice_free (RenderData, render);
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

static void
sp_line_visualizer_row_render_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  g_autoptr(GError) error = NULL;
  cairo_surface_t *surface;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (user_data == NULL);

  surface = sp_line_visualizer_row_render_finish (self, result, &error);

  if (surface == NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  g_clear_pointer (&priv->surface, cairo_surface_destroy);
  priv->surface = surface;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sp_line_visualizer_row_begin_offscreen_draw (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  if (priv->cache != NULL)
    sp_line_visualizer_row_render_async (self,
                                         priv->cache,
                                         NULL,
                                         sp_line_visualizer_row_render_cb,
                                         NULL);
}

static inline void
subtract_border (GtkAllocation *alloc,
                 GtkBorder     *border)
{
#if 0
  g_print ("Border; %d %d %d %d\n", border->top, border->left, border->bottom, border->right);
#endif

  alloc->x += border->left;
  alloc->y += border->top;
  alloc->width -= border->left + border->right;
  alloc->height -= border->top + border->bottom;
}

static void
adjust_alloc_for_borders (SpLineVisualizerRow *self,
                          GtkAllocation       *alloc)
{
  GtkStyleContext *style_context;
  GtkBorder border;
  GtkStateFlags state;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (alloc != NULL);

  state = gtk_widget_get_state_flags (GTK_WIDGET (self));
  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_get_border (style_context, state, &border);

  subtract_border (alloc, &border);
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
  adjust_alloc_for_borders (self, &alloc);

  ret = GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->draw (widget, cr);

  if (ret == GDK_EVENT_PROPAGATE && priv->surface != NULL)
    {
      gint scale_factor = gtk_widget_get_scale_factor (widget);
      gint width;
      gint height;

      width = cairo_image_surface_get_width (priv->surface) / scale_factor;
      height = cairo_image_surface_get_height (priv->surface) / scale_factor;

      /*
       * We must have another threaded draw queued, so to give the impression
       * that we are quickly growing with the widget, we will scale the draw
       * and then paint our wrongly sized surface (which will likely be a bit
       * blurred, but that's okay).
       */
      if (width != alloc.width || height != alloc.height)
        {
          cairo_rectangle (cr, 0, 0, width, height);
          cairo_scale (cr, (gdouble)alloc.width / (gdouble)width, (gdouble)alloc.height / (gdouble)height);
          cairo_set_source_surface (cr, priv->surface, 0, 0);
          cairo_fill (cr);
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
sp_line_visualizer_row_load_data_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  g_autoptr(GError) error = NULL;
  g_autoptr(PointCache) cache = NULL;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  cache = sp_line_visualizer_row_load_data_finish (self, result, &error);

  if (cache == NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  g_clear_pointer (&priv->cache, point_cache_unref);
  priv->cache = g_steal_pointer (&cache);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static gboolean
sp_line_visualizer_row_do_reload (gpointer data)
{
  SpLineVisualizerRow *self = data;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  priv->queued_load = 0;

  if (priv->reader != NULL)
    {
      sp_line_visualizer_row_load_data_async (self,
                                              NULL,
                                              sp_line_visualizer_row_load_data_cb,
                                              NULL);
    }

  return G_SOURCE_REMOVE;
}

static void
sp_line_visualizer_row_queue_reload (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  if (priv->queued_load == 0)
    {
      priv->queued_load = gdk_threads_add_idle_full (G_PRIORITY_LOW,
                                                     sp_line_visualizer_row_do_reload,
                                                     self,
                                                     NULL);
    }
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

      sp_line_visualizer_row_queue_reload (self);
    }
}

static void
sp_line_visualizer_row_finalize (GObject *object)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->lines, g_array_unref);
  g_clear_pointer (&priv->cache, point_cache_unref);
  g_clear_pointer (&priv->reader, sp_capture_reader_unref);

  if (priv->queued_load != 0)
    {
      g_source_remove (priv->queued_load);
      priv->queued_load = 0;
    }

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

  sp_line_visualizer_row_begin_offscreen_draw (self);
}

static void
sp_line_visualizer_row_style_updated (GtkWidget *widget)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->style_updated (widget);

  /*
   * Only queue a draw if a line that is drawn relies on the the style context
   * of the widget (as opposed to a style set manually).
   */
  for (guint i = 0; i < priv->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (priv->lines, LineInfo, i);

      if (line_info->use_default_style)
        {
          sp_line_visualizer_row_begin_offscreen_draw (self);
          break;
        }
    }
}

static void
sp_line_visualizer_row_set_time_range (SpVisualizerRow *row,
                                       gint64           begin_time,
                                       gint64           end_time)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)row;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (row));
  g_assert (begin_time <= end_time);

  sp_line_visualizer_row_queue_reload (self);
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

    case PROP_Y_LOWER:
      g_value_set_double (value, priv->y_lower);
      break;

    case PROP_Y_UPPER:
      g_value_set_double (value, priv->y_upper);
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

    case PROP_Y_LOWER:
      priv->y_lower = g_value_get_double (value);
      gtk_widget_queue_resize (GTK_WIDGET (self));
      break;

    case PROP_Y_UPPER:
      priv->y_upper = g_value_get_double (value);
      gtk_widget_queue_resize (GTK_WIDGET (self));
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

  object_class->finalize = sp_line_visualizer_row_finalize;
  object_class->get_property = sp_line_visualizer_row_get_property;
  object_class->set_property = sp_line_visualizer_row_set_property;

  widget_class->draw = sp_line_visualizer_row_draw;
  widget_class->destroy = sp_line_visualizer_row_destroy;
  widget_class->size_allocate = sp_line_visualizer_row_size_allocate;
  widget_class->style_updated = sp_line_visualizer_row_style_updated;

  visualizer_class->set_reader = sp_line_visualizer_row_set_reader;
  visualizer_class->set_time_range = sp_line_visualizer_row_set_time_range;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_Y_LOWER] =
    g_param_spec_double ("y-lower",
                         "Y Lower",
                         "The lowest Y value for the visualizer",
                         -G_MAXDOUBLE,
                         G_MAXDOUBLE,
                         0.0,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_Y_UPPER] =
    g_param_spec_double ("y-upper",
                         "Y Upper",
                         "The highest Y value for the visualizer",
                         -G_MAXDOUBLE,
                         G_MAXDOUBLE,
                         100.0,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_line_visualizer_row_init (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  PangoAttrList *attrs = pango_attr_list_new ();

  priv->lines = g_array_new (FALSE, FALSE, sizeof (LineInfo));

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
                                    guint                counter_id,
                                    const GdkRGBA       *color)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  LineInfo line_info = { 0 };

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (priv->lines != NULL);

  line_info.id = counter_id;
  line_info.line_width = 1.0;

  if (color != NULL)
    {
      line_info.foreground = *color;
      line_info.use_default_style = FALSE;
    }
  else
    {
      line_info.use_default_style = TRUE;
    }

  g_array_append_val (priv->lines, line_info);

  if (SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added)
    SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added (self, counter_id);

  sp_line_visualizer_row_queue_reload (self);
}

void
sp_line_visualizer_row_clear (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_LINE_VISUALIZER_ROW (self));

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

static inline guint8
counter_type (LoadData *load,
              guint     counter_id)
{
  /* TODO: Support other counter types
   *
   * We need to keep some information on the counter (by id) so that we
   * can track the counters type (which is a 1-byte type id).
   */
  return SP_CAPTURE_COUNTER_DOUBLE;
}

static gfloat
calc_x (gint64 lower,
        gint64 upper,
        gint64 value)
{
  return (gdouble)(value - lower) / (gdouble)(upper - lower);
}

static gfloat
calc_y_double (gdouble lower,
               gdouble upper,
               gdouble value)
{
  return (value - lower) / (upper - lower);
}

static gfloat
calc_y_int64 (gint64 lower,
              gint64 upper,
              gint64 value)
{
  return (gdouble)(value - lower) / (gdouble)(upper - lower);
}

static gboolean
sp_line_visualizer_row_load_data_frame_cb (const SpCaptureFrame *frame,
                                           gpointer              user_data)
{
  LoadData *load = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SP_CAPTURE_FRAME_CTRSET ||
            frame->type == SP_CAPTURE_FRAME_CTRDEF);
  g_assert (load != NULL);

  if (frame->type == SP_CAPTURE_FRAME_CTRSET)
    {
      const SpCaptureFrameCounterSet *set = (SpCaptureFrameCounterSet *)frame;
      gfloat x = calc_x (load->begin_time, load->end_time, frame->time);

      for (guint i = 0; i < set->n_values; i++)
        {
          const SpCaptureCounterValues *group = &set->values[i];

          for (guint j = 0; j < G_N_ELEMENTS (group->ids); j++)
            {
              guint counter_id = group->ids[j];

              if (counter_id != 0 && contains_id (load->lines, counter_id))
                {
                  gfloat y;

                  if (counter_type (load, counter_id) == SP_CAPTURE_COUNTER_DOUBLE)
                    y = calc_y_double (load->y_lower, load->y_upper, group->values[j].vdbl);
                  else
                    y = calc_y_int64 (load->y_lower, load->y_upper, group->values[j].v64);

                  point_cache_add_point_to_set (load->cache, counter_id, x, y);
                }
            }
        }
    }

  return TRUE;
}

static void
sp_line_visualizer_row_load_data_worker (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable)
{
  LoadData *load = task_data;
  g_autoptr(GArray) counter_ids = NULL;
  SpCaptureCondition *left, *right;
  gint64 ext_begin_time;
  gint64 ext_end_time;
  gint64 ext;

  g_assert (G_IS_TASK (task));
  g_assert (SP_IS_LINE_VISUALIZER_ROW (source_object));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  counter_ids = g_array_new (FALSE, FALSE, sizeof (guint));

  for (guint i = 0; i < load->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (load->lines, LineInfo, i);

      g_array_append_val (counter_ids, line_info->id);
    }

  /*
   * Add a little extra time to the visible range so that we can get any datapoints
   * that might be overlapping the region a bit. This helps so that we can draw
   * appropriate data points that need a proper x,y coordinate outside the
   * visible area for cairo_curve_to().
   */
  ext = (load->end_time - load->begin_time) / 3;
  ext_begin_time = load->begin_time - ext;
  ext_end_time = load->end_time + ext;

  left = sp_capture_condition_new_where_counter_in (counter_ids->len, (guint *)(gpointer)counter_ids->data);
  right = sp_capture_condition_new_where_time_between (ext_begin_time, ext_end_time);
  sp_capture_cursor_add_condition (load->cursor, sp_capture_condition_new_and (left, right));

  sp_capture_cursor_foreach (load->cursor, sp_line_visualizer_row_load_data_frame_cb, load);

  g_task_return_pointer (task, g_steal_pointer (&load->cache), (GDestroyNotify)point_cache_unref);
}

static void
sp_line_visualizer_row_load_data_async (SpLineVisualizerRow *self,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  g_autoptr(GTask) task = NULL;
  LoadData *load;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sp_line_visualizer_row_load_data_async);

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
  load->y_lower = priv->y_lower;
  load->y_upper = priv->y_upper;
  sp_visualizer_row_get_time_range (SP_VISUALIZER_ROW (self), &load->begin_time, &load->end_time);
  load->cursor = sp_capture_cursor_new (priv->reader);
  load->lines = copy_array (priv->lines);

  for (guint i = 0; i < load->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (load->lines, LineInfo, i);

      point_cache_add_set (load->cache, line_info->id);
    }

  g_task_set_task_data  (task, load, load_data_free);
  g_task_run_in_thread (task, sp_line_visualizer_row_load_data_worker);
}

static PointCache *
sp_line_visualizer_row_load_data_finish (SpLineVisualizerRow  *self,
                                         GAsyncResult         *result,
                                         GError              **error)
{
  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
sp_line_visualizer_row_render_worker (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  RenderData *render = task_data;
  cairo_surface_t *surface = NULL;
  cairo_t *cr = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SP_IS_LINE_VISUALIZER_ROW (source_object));
  g_assert (render != NULL);
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
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        render->width * render->scale_factor,
                                        render->height * render->scale_factor);

  if (surface == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to create image surface of size %dx%d",
                               render->width, render->height);
      goto cleanup;
    }

  if (render->scale_factor != 1)
    cairo_surface_set_device_scale (surface,
                                    render->scale_factor,
                                    render->scale_factor);

  cr = cairo_create (surface);

  if (cr == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to create cairo context");
      goto cleanup;
    }

  /* Flip the coordinate space so that 0,0 is in the bottom left. */
  cairo_translate (cr, 0, render->height);
  cairo_scale (cr, 1.0, -1.0);

  for (guint i = 0; i < render->lines->len; i++)
    {
      const LineInfo *line_info = &g_array_index (render->lines, LineInfo, i);
      const Point *points;
      guint n_points;

      points = point_cache_get_points (render->cache, line_info->id, &n_points);

      if (n_points > 0)
        {
          gdouble last_x = points[0].x * render->width;
          gdouble last_y = points[0].y * render->height;

          cairo_move_to (cr, last_x, last_y);

          for (guint j = 1; j < n_points; j++)
            {
              gdouble x = points[j].x * render->width;
              gdouble y = points[j].y * render->height;

              cairo_curve_to (cr,
                              last_x + ((x - last_x) / 2),
                              last_y,
                              last_x + ((x - last_x) / 2),
                              y,
                              x,
                              y);

              last_x = x;
              last_y = y;
            }

          cairo_set_line_width (cr, line_info->line_width);

          if (line_info->use_default_style)
            gdk_cairo_set_source_rgba (cr, &render->color);
          else
            gdk_cairo_set_source_rgba (cr, &line_info->foreground);

          cairo_stroke (cr);
        }
    }

  g_task_return_pointer (task,
                         g_steal_pointer (&surface),
                         (GDestroyNotify)cairo_surface_destroy);

cleanup:
  if (surface != NULL)
    cairo_surface_destroy (surface);

  if (cr != NULL)
    cairo_destroy (cr);
}

/**
 * sp_line_visualizer_row_render_async:
 * @self: an #SpLineVisualizerRow
 * @cache: (transfer full): A #PointCache with points to render
 * @lines: (transfer full): information about the lines
 * @n_lines: number of lines to render
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @callback: a callback for completion
 * @user_data: user data for @callback
 *
 * This asynchronously renders the visuzlier to an offscreen
 * cairo_image_surface_t which cna then be rendered to the front
 * buffer as necessary.
 *
 * Call sp_line_visualizer_row_render_finish() to get the rendered
 * image surface.
 */
static void
sp_line_visualizer_row_render_async (SpLineVisualizerRow *self,
                                     PointCache          *cache,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  RenderData *render;
  g_autoptr(GTask) task = NULL;
  GtkStyleContext *style_context;
  GtkAllocation alloc;
  GtkStateFlags state;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (cache != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);
  adjust_alloc_for_borders (self, &alloc);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sp_line_visualizer_row_render_async);

  render = g_slice_new0 (RenderData);
  render->cache = point_cache_ref (cache);
  render->lines = copy_array (priv->lines);
  render->width = alloc.width;
  render->height = alloc.height;
  render->scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));

  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  state = gtk_widget_get_state_flags (GTK_WIDGET (self));
  gtk_style_context_get_color (style_context, state, &render->color);

  g_task_set_task_data (task, render, render_data_free);
  g_task_run_in_thread (task, sp_line_visualizer_row_render_worker);
}

/**
 * sp_line_visualizer_row_render_finish:
 *
 * This completes an asynchronous request to render the visualizer.
 * The resulting cairo_surface_t will really be a cairo_image_surface_t.
 * We do not use a "similar surface" because we need to ensure that
 * the render can be performed in a thread-safe manner as it is
 * dispatched to another thread.
 *
 * Returns: (transfer full): A #cairo_image_surface_t.
 */
static cairo_surface_t *
sp_line_visualizer_row_render_finish (SpLineVisualizerRow  *self,
                                      GAsyncResult         *result,
                                      GError              **error)
{
  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_pointer (G_TASK (result), error);
}
