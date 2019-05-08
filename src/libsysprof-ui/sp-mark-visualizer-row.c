/* sp-mark-visualizer-row.c
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

#define G_LOG_DOMAIN "sp-mark-visualizer-row"

#include "config.h"

#include "sp-capture-condition.h"
#include "sp-capture-cursor.h"
#include "rectangles.h"
#include "sp-mark-visualizer-row.h"

typedef struct
{
  /*
   * Our reader as assigned by the visualizer system.
   */
  SpCaptureReader *reader;

  /*
   * The group we care about for displaying marks. The idea is that we only
   * show one group per-row, so tooling from separate systems can either be
   * displayed together or not, based on usefulness.
   */
  gchar *group;

  /*
   * Rectangle information we have built from the marks that belong to this
   * row of information.
   */
  Rectangles *rectangles;

  /*
   * Child widget to display the label in the upper corner.
   */
  GtkLabel *label;
} SpMarkVisualizerRowPrivate;

typedef struct
{
  gchar *group;
  SpCaptureCursor *cursor;
  Rectangles *rects;
  GHashTable *inferred_rects;
} BuildState;

typedef struct {
  gint64 time;
  gchar *name;
  gchar *message;
} InferredRect;

enum {
  PROP_0,
  PROP_GROUP,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpMarkVisualizerRow, sp_mark_visualizer_row, SP_TYPE_VISUALIZER_ROW)

static GParamSpec *properties [N_PROPS];

static void
free_inferred_rect (InferredRect *rect)
{
  g_free (rect->name);
  g_free (rect->message);
  g_slice_free (InferredRect, rect);
}

static void
add_inferred_rect_point (BuildState *state,
                         InferredRect *rect)
{
  rectangles_add (state->rects,
                  rect->time,
                  rect->time,
                  rect->name,
                  rect->message);
}

static void
build_state_free (BuildState *state)
{
  g_hash_table_remove_all (state->inferred_rects);
  g_clear_pointer (&state->inferred_rects, g_hash_table_unref);
  g_free (state->group);
  g_object_unref (state->cursor);
  g_slice_free (BuildState, state);
}

/* Creates rectangles for GPU marks.
 *
 * GPU marks come in as a begin and an end, but since those things are
 * processessed on potentially different CPUs, perf doesn't record
 * them in sequence order in the mmap ringbuffer.  Thus, we have to
 * shuffle things back around at visualization time.
 */
static gboolean
process_gpu_mark (BuildState *state,
                  const SpCaptureMark *mark)
{
  InferredRect *rect = g_hash_table_lookup (state->inferred_rects,
                                            mark->message);

  if (rect)
    {
      gboolean ours_begins = strstr (mark->name, "begin") != NULL;
      gboolean theirs_begins = strstr (rect->name, "begin") != NULL;

      if (ours_begins != theirs_begins)
        {
          rectangles_add (state->rects,
                          ours_begins ? mark->frame.time : rect->time,
                          ours_begins ? rect->time : mark->frame.time,
                          ours_begins ? mark->name : rect->name,
                          rect->message);
        }
      else
        {
          /* Something went weird with the tracking (GPU hang caused
           * two starts?), so just put up both time points as vertical
           * bars for now.
           */
          rectangles_add (state->rects,
                          mark->frame.time,
                          mark->frame.time,
                          mark->name,
                          mark->message);

          add_inferred_rect_point (state, rect);
        }

      g_hash_table_remove (state->inferred_rects,
                           rect->message);
    }
  else
    {
      rect = g_slice_new0 (InferredRect);
      rect->name = g_strdup (mark->name);
      rect->message = g_strdup (mark->message);
      rect->time = mark->frame.time;

      g_hash_table_insert (state->inferred_rects, rect->message, rect);
    }

  return TRUE;
}


static gboolean
sp_mark_visualizer_row_add_rect (const SpCaptureFrame *frame,
                                 gpointer              user_data)
{
  BuildState *state = user_data;
  const SpCaptureMark *mark = (const SpCaptureMark *)frame;

  g_assert (frame != NULL);
  g_assert (frame->type == SP_CAPTURE_FRAME_MARK);
  g_assert (state != NULL);
  g_assert (state->rects != NULL);

  if (g_strcmp0 (mark->group, state->group) == 0)
    {
      if (strstr (mark->name, "gpu begin") != NULL ||
          strstr (mark->name, "gpu end") != NULL)
        process_gpu_mark (state, mark);
      else
        rectangles_add (state->rects,
                        frame->time,
                        frame->time + mark->duration,
                        mark->name,
                        mark->message);
    }

  return TRUE;
}

static void
sp_mark_visualizer_row_worker (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
  BuildState *state = task_data;
  GHashTableIter iter;
  gpointer key, value;
  gint64 end_time;

  g_assert (G_IS_TASK (task));
  g_assert (SP_IS_MARK_VISUALIZER_ROW (source_object));
  g_assert (state != NULL);
  g_assert (state->cursor != NULL);

  sp_capture_cursor_foreach (state->cursor, sp_mark_visualizer_row_add_rect, state);

  /* If any inferred rects are left incomplete, just drop them in as
   * point events for now.
   */
  g_hash_table_iter_init (&iter, state->inferred_rects);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      InferredRect *rect = value;

      add_inferred_rect_point (state, rect);
    }
  g_hash_table_remove_all (state->inferred_rects);

  end_time = sp_capture_reader_get_end_time (sp_capture_cursor_get_reader (state->cursor));
  rectangles_set_end_time (state->rects, end_time);
  g_task_return_pointer (task, g_steal_pointer (&state->rects), (GDestroyNotify)rectangles_free);
}

static gboolean
sp_mark_visualizer_row_query_tooltip (GtkWidget  *widget,
                                      gint        x,
                                      gint        y,
                                      gboolean    keyboard_mode,
                                      GtkTooltip *tooltip)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)widget;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));

  if (priv->rectangles == NULL)
    return FALSE;

  return rectangles_query_tooltip (priv->rectangles, tooltip, priv->group, x, y);
}

static gboolean
sp_mark_visualizer_row_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)widget;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);
  GtkStyleContext *style_context;
  GtkStateFlags flags;
  GdkRGBA foreground;
  GtkAllocation alloc;
  gboolean ret;

  g_assert (SP_IS_MARK_VISUALIZER_ROW (widget));
  g_assert (cr != NULL);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sp_mark_visualizer_row_parent_class)->draw (widget, cr);

  if (priv->rectangles == NULL)
    return ret;

  style_context = gtk_widget_get_style_context (widget);
  flags = gtk_widget_get_state_flags (widget);
  gtk_style_context_get_color (style_context, flags, &foreground);

  rectangles_draw (priv->rectangles, GTK_WIDGET (self), cr);

  return ret;
}

static void
data_load_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)object;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (result));

  g_clear_pointer (&priv->rectangles, rectangles_free);
  priv->rectangles = g_task_propagate_pointer (G_TASK (result), NULL);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sp_mark_visualizer_row_reload (SpMarkVisualizerRow *self)
{
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);
  g_autoptr(SpCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  SpCaptureCondition *condition;
  BuildState *state;

  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));

  g_clear_pointer (&priv->rectangles, rectangles_free);

  condition = sp_capture_condition_new_where_type_in (1, (SpCaptureFrameType[]) { SP_CAPTURE_FRAME_MARK });
  cursor = sp_capture_cursor_new (priv->reader);
  sp_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  state = g_slice_new0 (BuildState);
  state->inferred_rects = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 NULL,
                                                 (GDestroyNotify)free_inferred_rect);
  state->group = g_strdup (priv->group);
  state->cursor = g_steal_pointer (&cursor);
  state->rects = rectangles_new (sp_capture_reader_get_start_time (priv->reader),
                                 sp_capture_reader_get_end_time (priv->reader));

  task = g_task_new (self, NULL, data_load_cb, NULL);
  g_task_set_task_data (task, state, (GDestroyNotify)build_state_free);
  g_task_run_in_thread (task, sp_mark_visualizer_row_worker);
}

static void
sp_mark_visualizer_row_set_reader (SpVisualizerRow *row,
                                   SpCaptureReader *reader)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)row;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_MARK_VISUALIZER_ROW (self));

  if (reader != priv->reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);
      if (reader != NULL)
        priv->reader = sp_capture_reader_ref (reader);
      sp_mark_visualizer_row_reload (self);
    }
}

static void
sp_mark_visualizer_row_finalize (GObject *object)
{
  SpMarkVisualizerRow *self = (SpMarkVisualizerRow *)object;
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->group, g_free);
  g_clear_pointer (&priv->rectangles, rectangles_free);

  G_OBJECT_CLASS (sp_mark_visualizer_row_parent_class)->finalize (object);
}

static void
sp_mark_visualizer_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpMarkVisualizerRow *self = SP_MARK_VISUALIZER_ROW (object);
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_string (value, sp_mark_visualizer_row_get_group (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, gtk_label_get_label (priv->label));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_mark_visualizer_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SpMarkVisualizerRow *self = SP_MARK_VISUALIZER_ROW (object);
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_GROUP:
      sp_mark_visualizer_row_set_group (self, g_value_get_string (value));
      break;

    case PROP_TITLE:
      gtk_label_set_label (priv->label, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_mark_visualizer_row_class_init (SpMarkVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SpVisualizerRowClass *visualizer_class = SP_VISUALIZER_ROW_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sp_mark_visualizer_row_finalize;
  object_class->get_property = sp_mark_visualizer_row_get_property;
  object_class->set_property = sp_mark_visualizer_row_set_property;

  widget_class->draw = sp_mark_visualizer_row_draw;
  widget_class->query_tooltip = sp_mark_visualizer_row_query_tooltip;

  visualizer_class->set_reader = sp_mark_visualizer_row_set_reader;

  properties [PROP_GROUP] =
    g_param_spec_string ("group",
                         "Group",
                         "The group of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_mark_visualizer_row_init (SpMarkVisualizerRow *self)
{
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);
  PangoAttrList *attrs = pango_attr_list_new ();

  gtk_widget_set_has_tooltip (GTK_WIDGET (self), TRUE);

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

GtkWidget *
sp_mark_visualizer_row_new (void)
{
  return g_object_new (SP_TYPE_MARK_VISUALIZER_ROW, NULL);
}

const gchar *
sp_mark_visualizer_row_get_group (SpMarkVisualizerRow *self)
{
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_return_val_if_fail (SP_IS_MARK_VISUALIZER_ROW (self), NULL);

  return priv->group;
}

void
sp_mark_visualizer_row_set_group (SpMarkVisualizerRow *self,
                                  const gchar         *group)
{
  SpMarkVisualizerRowPrivate *priv = sp_mark_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_MARK_VISUALIZER_ROW (self));

  if (g_strcmp0 (priv->group, group) != 0)
    {
      g_free (priv->group);
      priv->group = g_strdup (group);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_GROUP]);
    }
}
