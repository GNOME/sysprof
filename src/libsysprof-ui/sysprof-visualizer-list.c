/* sysprof-visualizer-list.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-visualizer-list"

#include "config.h"

#include <glib/gi18n.h>
#include <sysprof.h>

#include "sysprof-cpu-visualizer-row.h"
#include "sysprof-depth-visualizer-row.h"
#include "sysprof-visualizer-list.h"
#include "sysprof-visualizer-row.h"
#include "sysprof-mark-visualizer-row.h"
#include "sysprof-zoom-manager.h"

typedef struct
{
  SysprofCaptureReader *reader;
  SysprofZoomManager *zoom_manager;
} SysprofVisualizerListPrivate;

typedef struct
{
  SysprofCaptureCursor *cursor;
  GHashTable *mark_groups;
  guint fps_counter;
  guint pixels_counter;
  GArray *memory;
  guint has_cpu : 1;
  guint has_sample : 1;
} Discovery;

enum {
  PROP_0,
  PROP_READER,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofVisualizerList, sysprof_visualizer_list, GTK_TYPE_LIST_BOX)

static GParamSpec *properties [N_PROPS];

static void
discovery_free (Discovery *state)
{
  g_clear_pointer (&state->mark_groups, g_hash_table_unref);
  g_clear_pointer (&state->memory, g_array_unref);
  g_clear_pointer (&state->cursor, sysprof_capture_cursor_unref);
  g_slice_free (Discovery, state);
}

static void
sysprof_visualizer_list_add (GtkContainer *container,
                             GtkWidget    *widget)
{
  SysprofVisualizerList *self = (SysprofVisualizerList *)container;
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  GTK_CONTAINER_CLASS (sysprof_visualizer_list_parent_class)->add (container, widget);

  if (SYSPROF_IS_VISUALIZER_ROW (widget))
    {
      sysprof_visualizer_row_set_reader (SYSPROF_VISUALIZER_ROW (widget), priv->reader);
      sysprof_visualizer_row_set_zoom_manager (SYSPROF_VISUALIZER_ROW (widget), priv->zoom_manager);
    }
}

static void
sysprof_visualizer_list_finalize (GObject *object)
{
  SysprofVisualizerList *self = (SysprofVisualizerList *)object;
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);

  G_OBJECT_CLASS (sysprof_visualizer_list_parent_class)->finalize (object);
}

static void
sysprof_visualizer_list_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofVisualizerList *self = SYSPROF_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sysprof_visualizer_list_get_reader (self));
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sysprof_visualizer_list_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_list_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofVisualizerList *self = SYSPROF_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      sysprof_visualizer_list_set_reader (self, g_value_get_boxed (value));
      break;

    case PROP_ZOOM_MANAGER:
      sysprof_visualizer_list_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_list_class_init (SysprofVisualizerListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->finalize = sysprof_visualizer_list_finalize;
  object_class->get_property = sysprof_visualizer_list_get_property;
  object_class->set_property = sysprof_visualizer_list_set_property;

  container_class->add = sysprof_visualizer_list_add;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The capture reader",
                        SYSPROF_TYPE_CAPTURE_READER,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "The zoom manager",
                         SYSPROF_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_visualizer_list_init (SysprofVisualizerList *self)
{
}

GtkWidget *
sysprof_visualizer_list_new (void)
{
  return g_object_new (SYSPROF_TYPE_VISUALIZER_ROW, NULL);
}

/**
 * sysprof_visualizer_list_get_reader:
 *
 * Gets the reader that is being used for the #SysprofVisualizerList.
 *
 * Returns: (transfer none): An #SysprofCaptureReader
 */
SysprofCaptureReader *
sysprof_visualizer_list_get_reader (SysprofVisualizerList *self)
{
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_LIST (self), NULL);

  return priv->reader;
}

static gboolean
discover_new_rows_frame_cb (const SysprofCaptureFrame *frame,
                            gpointer                   user_data)
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

  if (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE)
    {
      state->has_sample = TRUE;
    }
  else if (frame->type == SYSPROF_CAPTURE_FRAME_MARK)
    {
      const SysprofCaptureMark *mark = (const SysprofCaptureMark *)frame;

      if (!g_hash_table_contains (state->mark_groups, mark->group))
        g_hash_table_add (state->mark_groups, g_strdup (mark->group));
    }
  else if (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF)
    {
      const SysprofCaptureFrameCounterDefine *def = (const SysprofCaptureFrameCounterDefine *)frame;

      for (guint i = 0; i < def->n_counters; i++)
        {
          const SysprofCaptureCounter *ctr = &def->counters[i];

          if (!state->has_cpu &&
              strstr (ctr->category, "CPU Percent") != NULL)
            state->has_cpu = TRUE;
          else if (!state->fps_counter &&
                   strstr (ctr->category, "gtk") != NULL && strstr (ctr->name, "fps") != NULL)
            state->fps_counter = ctr->id;
          else if (!state->pixels_counter &&
                   strstr (ctr->category, "gtk") != NULL && strstr (ctr->name, "frame pixels") != NULL)
            state->pixels_counter = ctr->id;
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
  g_assert (state->cursor != NULL);

  sysprof_capture_cursor_foreach (state->cursor, discover_new_rows_frame_cb, state);
  g_task_return_boolean (task, TRUE);
}

static void
handle_capture_results (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  SysprofVisualizerList *self = (SysprofVisualizerList *)object;
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);
  Discovery *state;
  const gchar *key;

  g_assert (SYSPROF_IS_VISUALIZER_LIST (self));
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
      GtkWidget *row = g_object_new (SYSPROF_TYPE_CPU_VISUALIZER_ROW,
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

  if (state->has_sample)
    {
      GtkWidget *row = g_object_new (SYSPROF_TYPE_DEPTH_VISUALIZER_ROW,
                                     "zoom-manager", priv->zoom_manager,
                                     "height-request", 50,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     NULL);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  for (guint i = 0; i < state->memory->len; i++)
    {
      guint counter_id = g_array_index (state->memory, guint, i);
      GdkRGBA rgba;
      GtkWidget *row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER_ROW,
                                     "title", _("Memory Used"),
                                     "height-request", 35,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     NULL);
      gdk_rgba_parse (&rgba, "#204a87");
      sysprof_line_visualizer_row_add_counter (SYSPROF_LINE_VISUALIZER_ROW (row), counter_id, &rgba);
      rgba.alpha = 0.3;
      sysprof_line_visualizer_row_set_fill (SYSPROF_LINE_VISUALIZER_ROW (row), counter_id, &rgba);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  if (state->fps_counter)
    {
      GdkRGBA rgba;
      GtkWidget *row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER_ROW,
                                     /* Translators: FPS is frames per second. */
                                     "title", _("FPS"),
                                     "height-request", 35,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     "y-upper", 150.0,
                                     NULL);
      gdk_rgba_parse (&rgba, "#204a87");
      sysprof_line_visualizer_row_add_counter (SYSPROF_LINE_VISUALIZER_ROW (row), state->fps_counter, &rgba);
      rgba.alpha = 0.3;
      sysprof_line_visualizer_row_set_fill (SYSPROF_LINE_VISUALIZER_ROW (row), state->fps_counter, &rgba);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  if (state->pixels_counter)
    {
      GdkRGBA rgba;
      GtkWidget *row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER_ROW,
                                     /* Translators: amount of pixels drawn per frame. */
                                     "title", _("Pixel Bandwidth"),
                                     "height-request", 35,
                                     "selectable", FALSE,
                                     "visible", TRUE,
                                     "y-lower", 0.0,
                                     NULL);
      gdk_rgba_parse (&rgba, "#ad7fa8");
      sysprof_line_visualizer_row_add_counter (SYSPROF_LINE_VISUALIZER_ROW (row), state->pixels_counter, &rgba);
      rgba.alpha = 0.3;
      sysprof_line_visualizer_row_set_fill (SYSPROF_LINE_VISUALIZER_ROW (row), state->pixels_counter, &rgba);
      gtk_container_add (GTK_CONTAINER (self), row);
    }

  if (g_hash_table_size (state->mark_groups) < 30)
    {
      GHashTableIter iter;

      g_hash_table_iter_init (&iter, state->mark_groups);

      while (g_hash_table_iter_next (&iter, (gpointer *)&key, NULL))
        {
          GtkWidget *row = g_object_new (SYSPROF_TYPE_MARK_VISUALIZER_ROW,
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
discover_new_rows (SysprofVisualizerList *self,
                   SysprofCaptureReader  *reader)
{
  static const SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_CTRDEF,
    SYSPROF_CAPTURE_FRAME_MARK,
    SYSPROF_CAPTURE_FRAME_SAMPLE,
  };
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  SysprofCaptureCondition *condition;
  Discovery *state;

  g_assert (SYSPROF_IS_VISUALIZER_LIST (self));
  g_assert (reader != NULL);

  /*
   * The goal here is to automatically discover what rows should be added to
   * the visualizer list based on events we find in the capture file. In the
   * future, we might be able to add a UI flow to ask what the user wants or
   * denote capabilities at the beginning of the capture stream.
   */

  cursor = sysprof_capture_cursor_new (reader);
  condition = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  state = g_slice_new0 (Discovery);
  state->mark_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  state->cursor = g_steal_pointer (&cursor);
  state->memory = g_array_new (FALSE, FALSE, sizeof (guint));

  task = g_task_new (self, NULL, handle_capture_results, NULL);
  g_task_set_task_data (task, g_steal_pointer (&state), (GDestroyNotify)discovery_free);
  g_task_run_in_thread (task, discover_new_rows_worker);
}

void
sysprof_visualizer_list_set_reader (SysprofVisualizerList *self,
                               SysprofCaptureReader  *reader)
{
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_LIST (self));

  if (reader != priv->reader)
    {
      g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);

      if (reader != NULL)
        {
          priv->reader = sysprof_capture_reader_ref (reader);
          discover_new_rows (self, reader);
        }

      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sysprof_visualizer_row_set_reader,
                             reader);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}

void
sysprof_visualizer_list_set_zoom_manager (SysprofVisualizerList *self,
                                     SysprofZoomManager    *zoom_manager)
{
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_LIST (self));
  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (zoom_manager));

  if (g_set_object (&priv->zoom_manager, zoom_manager))
    {
      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sysprof_visualizer_row_set_zoom_manager,
                             zoom_manager);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
    }
}

/**
 * sysprof_visualizer_list_get_zoom_manager:
 *
 * Returns: (nullable) (transfer): A #SysprofZoomManager or %NULL.
 */
SysprofZoomManager *
sysprof_visualizer_list_get_zoom_manager (SysprofVisualizerList *self)
{
  SysprofVisualizerListPrivate *priv = sysprof_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_LIST (self), NULL);

  return priv->zoom_manager;
}
