/* sp-visualizer-list.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-visualizer-list"

#include <glib/gi18n.h>

#include "capture/sp-capture-condition.h"
#include "capture/sp-capture-cursor.h"
#include "visualizers/sp-cpu-visualizer-row.h"
#include "visualizers/sp-visualizer-list.h"
#include "visualizers/sp-visualizer-row.h"
#include "visualizers/sp-mark-visualizer-row.h"
#include "util/sp-zoom-manager.h"

#define NSEC_PER_SEC              G_GUINT64_CONSTANT(1000000000)
#define DEFAULT_PIXELS_PER_SECOND 20

typedef struct
{
  SpCaptureReader *reader;
  SpZoomManager *zoom_manager;
  gint64 begin_time;
  gint64 end_time;
} SpVisualizerListPrivate;

typedef struct
{
  SpCaptureCursor *cursor;
  GHashTable *mark_groups;
  guint fps_counter;
  GArray *memory;
  guint has_cpu : 1;
} Discovery;

enum {
  PROP_0,
  PROP_READER,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpVisualizerList, sp_visualizer_list, GTK_TYPE_LIST_BOX)

static GParamSpec *properties [N_PROPS];

static void
discovery_free (Discovery *state)
{
  g_clear_pointer (&state->mark_groups, g_hash_table_unref);
  g_clear_pointer (&state->memory, g_array_unref);
  g_clear_object (&state->cursor);
  g_slice_free (Discovery, state);
}

static void
sp_visualizer_list_add (GtkContainer *container,
                        GtkWidget    *widget)
{
  SpVisualizerList *self = (SpVisualizerList *)container;
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  GTK_CONTAINER_CLASS (sp_visualizer_list_parent_class)->add (container, widget);

  if (SP_IS_VISUALIZER_ROW (widget))
    {
      sp_visualizer_row_set_reader (SP_VISUALIZER_ROW (widget), priv->reader);
      sp_visualizer_row_set_zoom_manager (SP_VISUALIZER_ROW (widget), priv->zoom_manager);
    }
}

static void
sp_visualizer_list_finalize (GObject *object)
{
  SpVisualizerList *self = (SpVisualizerList *)object;
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_clear_pointer (&priv->reader, sp_capture_reader_unref);

  G_OBJECT_CLASS (sp_visualizer_list_parent_class)->finalize (object);
}

static void
sp_visualizer_list_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SpVisualizerList *self = SP_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sp_visualizer_list_get_reader (self));
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sp_visualizer_list_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_list_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SpVisualizerList *self = SP_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      sp_visualizer_list_set_reader (self, g_value_get_boxed (value));
      break;

    case PROP_ZOOM_MANAGER:
      sp_visualizer_list_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_list_class_init (SpVisualizerListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->finalize = sp_visualizer_list_finalize;
  object_class->get_property = sp_visualizer_list_get_property;
  object_class->set_property = sp_visualizer_list_set_property;

  container_class->add = sp_visualizer_list_add;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The capture reader",
                        SP_TYPE_CAPTURE_READER,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "The zoom manager",
                         SP_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_visualizer_list_init (SpVisualizerList *self)
{
}

GtkWidget *
sp_visualizer_list_new (void)
{
  return g_object_new (SP_TYPE_VISUALIZER_ROW, NULL);
}

/**
 * sp_visualizer_list_get_reader:
 *
 * Gets the reader that is being used for the #SpVisualizerList.
 *
 * Returns: (transfer none): An #SpCaptureReader
 */
SpCaptureReader *
sp_visualizer_list_get_reader (SpVisualizerList *self)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_LIST (self), NULL);

  return priv->reader;
}

static gboolean
discover_new_rows_frame_cb (const SpCaptureFrame *frame,
                            gpointer              user_data)
{
  Discovery *state = user_data;

  g_assert (frame != NULL);
  g_assert (state != NULL);

  /*
   * NOTE:
   *
   * It would be nice if we could redesign this all around the concept of
   * an "gadget" or something which combines a data collection series
   * and widget views to be displayed.
   */

  if (frame->type == SP_CAPTURE_FRAME_MARK)
    {
      const SpCaptureMark *mark = (const SpCaptureMark *)frame;

      if (!g_hash_table_contains (state->mark_groups, mark->group))
        g_hash_table_add (state->mark_groups, g_strdup (mark->group));
    }

  if (frame->type == SP_CAPTURE_FRAME_CTRDEF)
    {
      const SpCaptureFrameCounterDefine *def = (const SpCaptureFrameCounterDefine *)frame;

      for (guint i = 0; i < def->n_counters; i++)
        {
          const SpCaptureCounter *ctr = &def->counters[i];

          if (!state->has_cpu &&
              strstr (ctr->category, "CPU Percent") != NULL)
            state->has_cpu = TRUE;
          else if (!state->fps_counter &&
                   strstr (ctr->category, "gtk") != NULL && strstr (ctr->name, "fps") != NULL)
            state->fps_counter = ctr->id;
          else if (strcmp ("Memory", ctr->category) == 0 &&
                   strcmp ("Used", ctr->name) == 0)
            {
              guint counter_id = ctr->id;
              g_array_append_val (state->memory, counter_id);
            }
        }
    }

  return TRUE;
}

static void
discover_new_rows_worker (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
  Discovery *state = task_data;

  g_assert (state != NULL);
  g_assert (SP_IS_CAPTURE_CURSOR (state->cursor));

  sp_capture_cursor_foreach (state->cursor, discover_new_rows_frame_cb, state);
  g_task_return_boolean (task, TRUE);
}

static void
handle_capture_results (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  SpVisualizerList *self = (SpVisualizerList *)object;
  Discovery *state;
  const gchar *key;

  g_assert (SP_IS_VISUALIZER_LIST (self));
  g_assert (G_IS_TASK (result));
  g_assert (user_data == NULL);

  state = g_task_get_task_data (G_TASK (result));

  /*
   * TODO: It would be really nice if we had a more structured way to do this
   *       so that data collections and visualizations could be mapped
   *       together. One way to do so might be to create the concept of an
   *       "instrument" which represents that pair and allows the user to
   *       select what sort of data collections they'd like to see.
   */

  if (state->has_cpu)
    {
      GtkWidget *row = g_object_new (SP_TYPE_CPU_VISUALIZER_ROW,
                                     /* Translators: CPU is the processor. */
                                     "title", _("CPU"),
                                     "height-request", 50,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     "y-upper", 100.0,
                                     NULL);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  for (guint i = 0; i < state->memory->len; i++)
    {
      guint counter_id = g_array_index (state->memory, guint, i);
      GdkRGBA rgba;
      GtkWidget *row = g_object_new (SP_TYPE_LINE_VISUALIZER_ROW,
                                     "title", _("Memory Used"),
                                     "height-request", 35,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     NULL);
      gdk_rgba_parse (&rgba, "#204a87");
      sp_line_visualizer_row_add_counter (SP_LINE_VISUALIZER_ROW (row), counter_id, &rgba);
      rgba.alpha = 0.3;
      sp_line_visualizer_row_set_fill (SP_LINE_VISUALIZER_ROW (row), counter_id, &rgba);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  if (state->fps_counter)
    {
      GdkRGBA rgba;
      GtkWidget *row = g_object_new (SP_TYPE_LINE_VISUALIZER_ROW,
                                     /* Translators: FPS is frames per second. */
                                     "title", _("FPS"),
                                     "height-request", 35,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     "y-upper", 150.0,
                                     NULL);
      gdk_rgba_parse (&rgba, "#204a87");
      sp_line_visualizer_row_add_counter (SP_LINE_VISUALIZER_ROW (row), state->fps_counter, &rgba);
      rgba.alpha = 0.3;
      sp_line_visualizer_row_set_fill (SP_LINE_VISUALIZER_ROW (row), state->fps_counter, &rgba);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  if (g_hash_table_size (state->mark_groups) < 30)
    {
      GHashTableIter iter;

      g_hash_table_iter_init (&iter, state->mark_groups);

      while (g_hash_table_iter_next (&iter, (gpointer *)&key, NULL))
        {
          GtkWidget *row = g_object_new (SP_TYPE_MARK_VISUALIZER_ROW,
                                         "group", key,
                                         "title", key,
                                         "height-request", 50,
                                         "selectable", FALSE,
                                         "visible", TRUE,
                                         NULL);
          gtk_container_add (GTK_CONTAINER (self), row);
        }
    }
}

static void
discover_new_rows (SpVisualizerList *self,
                   SpCaptureReader  *reader)
{
  static const SpCaptureFrameType types[] = { SP_CAPTURE_FRAME_CTRDEF, SP_CAPTURE_FRAME_MARK };
  g_autoptr(SpCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  SpCaptureCondition *condition;
  Discovery *state;

  g_assert (SP_IS_VISUALIZER_LIST (self));
  g_assert (reader != NULL);

  /*
   * The goal here is to automatically discover what rows should be added to
   * the visualizer list based on events we find in the capture file. In the
   * future, we might be able to add a UI flow to ask what the user wants or
   * denote capabilities at the beginning of the capture stream.
   */

  cursor = sp_capture_cursor_new (reader);
  condition = sp_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  sp_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  state = g_slice_new0 (Discovery);
  state->mark_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  state->cursor = g_steal_pointer (&cursor);
  state->memory = g_array_new (FALSE, FALSE, sizeof (guint));

  task = g_task_new (self, NULL, handle_capture_results, NULL);
  g_task_set_task_data (task, g_steal_pointer (&state), (GDestroyNotify)discovery_free);
  g_task_run_in_thread (task, discover_new_rows_worker);
}

void
sp_visualizer_list_set_reader (SpVisualizerList *self,
                               SpCaptureReader  *reader)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));

  if (reader != priv->reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);

      if (reader != NULL)
        {
          priv->reader = sp_capture_reader_ref (reader);
          discover_new_rows (self, reader);
        }

      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sp_visualizer_row_set_reader,
                             reader);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}

void
sp_visualizer_list_set_zoom_manager (SpVisualizerList *self,
                                     SpZoomManager    *zoom_manager)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));
  g_return_if_fail (SP_IS_ZOOM_MANAGER (zoom_manager));

  if (g_set_object (&priv->zoom_manager, zoom_manager))
    {
      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sp_visualizer_row_set_zoom_manager,
                             zoom_manager);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
    }
}

/**
 * sp_visualizer_list_get_zoom_manager:
 *
 * Returns: (nullable) (transfer): A #SpZoomManager or %NULL.
 */
SpZoomManager *
sp_visualizer_list_get_zoom_manager (SpVisualizerList *self)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_LIST (self), NULL);

  return priv->zoom_manager;
}
