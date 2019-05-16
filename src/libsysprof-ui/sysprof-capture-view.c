/* sysprof-capture-view.c
 *
 * Copyright 2019 Christian Hergert <unknown@domain.org>
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

#define G_LOG_DOMAIN "sysprof-capture-view"

#include "config.h"

#include "sysprof-callgraph-view.h"
#include "sysprof-capture-view.h"
#include "sysprof-details-view.h"
#include "sysprof-marks-view.h"
#include "sysprof-ui-private.h"
#include "sysprof-visualizer-view.h"

typedef struct
{
  gint64 begin_time;
  gint64 end_time;
  guint has_samples : 1;
  guint has_counters : 1;
  guint has_marks : 1;
} SysprofCaptureFeatures;

typedef struct
{
  SysprofCaptureReader   *reader;
  GCancellable           *cancellable;

  SysprofCaptureFeatures  features;

  /* Template Objects */
  GtkAdjustment          *time_adj;
  GtkStack               *stack;
  SysprofCallgraphView   *callgraph_view;
  SysprofDetailsView     *details_view;
  SysprofMarksView       *marks_view;
  SysprofVisualizerView  *visualizer_view;
  SysprofZoomManager     *zoom_manager;
  GtkStackSwitcher       *stack_switcher;

  guint                   busy;
} SysprofCaptureViewPrivate;

typedef struct
{
  SysprofCaptureReader *reader;
  SysprofSelection *selection;
  gint n_active;
  guint has_error : 1;
} LoadAsync;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofCaptureView, sysprof_capture_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_BUSY,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
load_async_free (gpointer data)
{
  LoadAsync *state = data;

  if (state != NULL)
    {
      g_clear_pointer (&state->reader, sysprof_capture_reader_unref);
      g_clear_object (&state->selection);
      g_slice_free (LoadAsync, state);
    }
}

/**
 * sysprof_capture_view_new:
 *
 * Create a new #SysprofCaptureView.
 *
 * Returns: (transfer full): a newly created #SysprofCaptureView
 *
 * Since: 3.34
 */
GtkWidget *
sysprof_capture_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_CAPTURE_VIEW, NULL);
}

static void
sysprof_capture_view_task_completed (SysprofCaptureView *self,
                                     GParamSpec         *pspec,
                                     GTask              *task)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (task));

  g_signal_handlers_disconnect_by_func (task,
                                        G_CALLBACK (sysprof_capture_view_task_completed),
                                        self);

  priv->busy--;

  if (priv->busy == 0)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY]);
}

static void
sysprof_capture_view_monitor_task (SysprofCaptureView *self,
                                   GTask              *task)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (task));

  priv->busy++;

  if (priv->busy == 1)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY]);

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (sysprof_capture_view_task_completed),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
sysprof_capture_view_generate_callgraph_cb (GObject      *object,
                                            GAsyncResult *result,
                                            gpointer      user_data)
{
  SysprofCallgraphProfile *callgraph = (SysprofCallgraphProfile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofCaptureView *self;
  SysprofCaptureViewPrivate *priv;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (callgraph));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!sysprof_profile_generate_finish (SYSPROF_PROFILE (callgraph), result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  self = g_task_get_source_object (task);
  priv = sysprof_capture_view_get_instance_private (self);

  sysprof_callgraph_view_set_profile (priv->callgraph_view, callgraph);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_capture_view_generate_callgraph_async (SysprofCaptureView   *self,
                                               SysprofCaptureReader *reader,
                                               SysprofSelection     *selection,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
  g_autoptr(SysprofCaptureReader) copy = NULL;
  g_autoptr(SysprofProfile) callgraph = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_CAPTURE_VIEW (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_capture_view_generate_callgraph_async);
  sysprof_capture_view_monitor_task (self, task);

  copy = sysprof_capture_reader_copy (reader);
  callgraph = sysprof_callgraph_profile_new_with_selection (selection);
  sysprof_profile_set_reader (callgraph, copy);
  sysprof_profile_generate (callgraph,
                            cancellable,
                            sysprof_capture_view_generate_callgraph_cb,
                            g_steal_pointer (&task));
}

static gboolean
sysprof_capture_view_generate_callgraph_finish (SysprofCaptureView  *self,
                                                GAsyncResult        *result,
                                                GError             **error)
{
  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_capture_view_scan_worker (GTask        *task,
                                  gpointer      source_object,
                                  gpointer      task_data,
                                  GCancellable *cancellable)
{
  SysprofCaptureView *self = source_object;
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);
  SysprofCaptureReader *reader = task_data;
  SysprofCaptureFeatures features = {0};
  SysprofCaptureFrame frame;
  SysprofCaptureStat st_buf = {{0}};

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (task));
  g_assert (reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  features.begin_time = sysprof_capture_reader_get_start_time (reader);
  features.end_time = sysprof_capture_reader_get_end_time (reader);

  while (sysprof_capture_reader_peek_frame (reader, &frame))
    {
      gint64 begin_time = frame.time;
      gint64 end_time = G_MININT64;

      g_assert (frame.type < G_N_ELEMENTS (st_buf.frame_count));

      st_buf.frame_count[frame.type]++;

      if (frame.type == SYSPROF_CAPTURE_FRAME_MARK)
        {
          const SysprofCaptureMark *mark;
          if ((mark = sysprof_capture_reader_read_mark (reader)))
            end_time = frame.time + mark->duration;

          features.has_marks = TRUE;
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_CTRDEF)
        {
          features.has_counters = TRUE;
          sysprof_capture_reader_skip (reader);
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          features.has_samples = TRUE;
          sysprof_capture_reader_skip (reader);
        }
      else
        {
          sysprof_capture_reader_skip (reader);
        }

      if (begin_time < features.begin_time)
        features.begin_time = begin_time;

      if (end_time > features.end_time)
        features.end_time = end_time;
    }

  sysprof_capture_reader_set_stat (reader, &st_buf);

  if (!g_task_return_error_if_cancelled (task))
    {
      priv->features = features;
      sysprof_capture_reader_reset (reader);
      g_task_return_boolean (task, TRUE);
    }
}

static void
sysprof_capture_view_scan_async (SysprofCaptureView   *self,
                                 SysprofCaptureReader *reader,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_capture_view_scan_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_ref (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);
  sysprof_capture_view_monitor_task (self, task);
  g_task_run_in_thread (task, sysprof_capture_view_scan_worker);
}

static gboolean
sysprof_capture_view_scan_finish (SysprofCaptureView  *self,
                                  GAsyncResult        *result,
                                  GError             **error)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (result));

  if (!priv->features.has_samples && priv->features.has_marks)
    gtk_stack_set_visible_child_name (priv->stack, "timings");

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_capture_view_load_marks_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  SysprofMarksView *view = (SysprofMarksView *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  LoadAsync *state;

  g_assert (SYSPROF_IS_MARKS_VIEW (view));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  state = g_task_get_task_data (task);
  g_assert (state != NULL);
  g_assert (state->reader != NULL);
  g_assert (state->n_active > 0);

  state->n_active--;

  if (!sysprof_marks_view_load_finish (view, result, &error))
    {
      if (!state->has_error)
        {
          state->has_error = TRUE;
          g_task_return_error (task, g_steal_pointer (&error));
        }

      return;
    }

  if (state->n_active == 0)
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_capture_view_load_callgraph_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  SysprofCaptureView *self = (SysprofCaptureView *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  LoadAsync *state;

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  state = g_task_get_task_data (task);
  g_assert (state != NULL);
  g_assert (state->reader != NULL);
  g_assert (state->n_active > 0);

  state->n_active--;

  if (!sysprof_capture_view_generate_callgraph_finish (self, result, &error))
    {
      if (!state->has_error)
        {
          state->has_error = TRUE;
          g_task_return_error (task, g_steal_pointer (&error));
        }

      return;
    }

  if (state->n_active == 0)
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_capture_view_load_scan_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  SysprofCaptureView *self = (SysprofCaptureView *)object;
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  LoadAsync *state;

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!sysprof_capture_view_scan_finish (self, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  state = g_task_get_task_data (task);
  g_assert (state != NULL);
  g_assert (state->reader != NULL);

  if (priv->features.has_samples)
    {
      state->n_active++;
      sysprof_capture_view_generate_callgraph_async (self,
                                                     state->reader,
                                                     state->selection,
                                                     g_task_get_cancellable (task),
                                                     sysprof_capture_view_load_callgraph_cb,
                                                     g_object_ref (task));
    }

  if (priv->features.has_counters || priv->features.has_marks)
    sysprof_visualizer_view_set_reader (priv->visualizer_view, state->reader);

  state->n_active++;
  sysprof_marks_view_load_async (priv->marks_view,
                                 state->reader,
                                 state->selection,
                                 g_task_get_cancellable (task),
                                 sysprof_capture_view_load_marks_cb,
                                 g_object_ref (task));

  sysprof_details_view_set_reader (priv->details_view, priv->reader);

  if (state->n_active == 0)
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_capture_view_real_load_async (SysprofCaptureView   *self,
                                      SysprofCaptureReader *reader,
                                      GCancellable         *cancellable,
                                      GAsyncReadyCallback   callback,
                                      gpointer              user_data)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);
  g_autoptr(GTask) task = NULL;
  SysprofSelection *selection;
  LoadAsync *state;

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);
  priv->reader = sysprof_capture_reader_ref (reader);

  selection = sysprof_visualizer_view_get_selection (priv->visualizer_view);

  state = g_slice_new0 (LoadAsync);
  state->reader = sysprof_capture_reader_copy (reader);
  state->selection = g_object_ref (selection);
  state->n_active = 0;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_capture_view_real_load_async);
  g_task_set_task_data (task,
                        g_steal_pointer (&state),
                        load_async_free);
  sysprof_capture_view_monitor_task (self, task);

  /* Zoom after loading state */
  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (sysprof_capture_view_fit_to_width),
                           self,
                           G_CONNECT_SWAPPED);

  /* Cancel any previously in-flight operations and save a cancellable so
   * that any supplimental calls will result in the previous being cancelled.
   */
  if (priv->cancellable != cancellable)
    {
      g_cancellable_cancel (priv->cancellable);
      g_clear_object (&priv->cancellable);
      g_set_object (&priv->cancellable, cancellable);
    }

  /* First, discover the the time range for the display */
  sysprof_capture_view_scan_async (self,
                                   reader,
                                   cancellable,
                                   sysprof_capture_view_load_scan_cb,
                                   g_steal_pointer (&task));
}

static gboolean
sysprof_capture_view_real_load_finish (SysprofCaptureView  *self,
                                       GAsyncResult        *result,
                                       GError             **error)
{
  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (G_IS_TASK (result));
  
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_capture_view_selection_changed_cb (SysprofCaptureView *self,
                                           SysprofSelection   *selection)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));
  g_assert (SYSPROF_IS_SELECTION (selection));

  if (priv->reader == NULL)
    return;

  sysprof_capture_view_generate_callgraph_async (self,
                                                 priv->reader,
                                                 selection,
                                                 NULL, NULL, NULL);
  sysprof_marks_view_load_async (priv->marks_view,
                                 priv->reader,
                                 selection,
                                 NULL, NULL, NULL);
}

static void
fit_zoom_cb (GSimpleAction *action,
             GVariant      *param,
             gpointer       user_data)
{
  SysprofCaptureView *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));

  sysprof_capture_view_fit_to_width (self);
}

static void
set_use_underline_cb (GtkWidget *widget,
                      gpointer   user_data)
{
  if (GTK_IS_RADIO_BUTTON (widget))
    {
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

      if (GTK_IS_LABEL (child))
        gtk_label_set_use_underline (GTK_LABEL (child), TRUE);
    }
}

static gboolean
do_best_fit_in_idle (gpointer user_data)
{
  SysprofCaptureView *self = user_data;

  if (gtk_widget_get_visible (GTK_WIDGET (self)))
    sysprof_capture_view_fit_to_width (self);

  return G_SOURCE_REMOVE;
}

static void
sysprof_capture_view_map (GtkWidget *widget)
{
  SysprofCaptureView *self = (SysprofCaptureView *)widget;
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_assert (SYSPROF_IS_CAPTURE_VIEW (self));

  GTK_WIDGET_CLASS (sysprof_capture_view_parent_class)->map (widget);

  if (priv->reader != NULL)
    g_timeout_add_full (G_PRIORITY_LOW,
                        25,
                        do_best_fit_in_idle,
                        g_object_ref (self),
                        g_object_unref);
}

static void
sysprof_capture_view_finalize (GObject *object)
{
  SysprofCaptureView *self = (SysprofCaptureView *)object;
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);
  g_clear_object (&priv->cancellable);

  G_OBJECT_CLASS (sysprof_capture_view_parent_class)->finalize (object);
}

static void
sysprof_capture_view_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofCaptureView *self = (SysprofCaptureView *)object;

  switch (prop_id)
    {
    case PROP_BUSY:
      g_value_set_boolean (value, sysprof_capture_view_get_busy (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_capture_view_class_init (SysprofCaptureViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_capture_view_finalize;
  object_class->get_property = sysprof_capture_view_get_property;

  widget_class->map = sysprof_capture_view_map;

  klass->load_async = sysprof_capture_view_real_load_async;
  klass->load_finish = sysprof_capture_view_real_load_finish;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-capture-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, callgraph_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, details_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, marks_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, stack);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, stack_switcher);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, time_adj);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, visualizer_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCaptureView, zoom_manager);

  properties [PROP_BUSY] =
    g_param_spec_boolean ("busy",
                          "Busy",
                          "If the widget is busy",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (SYSPROF_TYPE_DETAILS_VIEW);
}

static void
sysprof_capture_view_init (SysprofCaptureView *self)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);
  g_autoptr(GSimpleActionGroup) group = NULL;
  SysprofSelection *selection;
  static GActionEntry actions[] = {
    { "fit-zoom", fit_zoom_cb },
  };

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_container_foreach (GTK_CONTAINER (priv->stack_switcher),
                         set_use_underline_cb,
                         NULL);

  selection = sysprof_visualizer_view_get_selection (priv->visualizer_view);
  g_signal_connect_object (selection,
                           "changed",
                           G_CALLBACK (sysprof_capture_view_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "zoom",
                                  G_ACTION_GROUP (priv->zoom_manager));

  group = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (group),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "capture-view",
                                  G_ACTION_GROUP (group));

  _sysprof_marks_view_set_hadjustment (priv->marks_view, priv->time_adj);
  _sysprof_visualizer_view_set_hadjustment (priv->visualizer_view, priv->time_adj);
}

/**
 * sysprof_capture_view_load_async:
 * @self: a #SysprofCaptureView
 *
 * Asynchronously loads a capture.
 *
 * Since: 3.34
 */
void
sysprof_capture_view_load_async (SysprofCaptureView   *self,
                                 SysprofCaptureReader *reader,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
  g_return_if_fail (SYSPROF_IS_CAPTURE_VIEW (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  SYSPROF_CAPTURE_VIEW_GET_CLASS (self)->load_async (self, reader, cancellable, callback, user_data);
}

/**
 * sysprof_capture_view_load_finish:
 * @self: a #SysprofCaptureView
 *
 * Completes a request to load a capture.
 *
 * Since: 3.34
 */
gboolean
sysprof_capture_view_load_finish (SysprofCaptureView  *self,
                                  GAsyncResult        *result,
                                  GError             **error)
{
  g_return_val_if_fail (SYSPROF_IS_CAPTURE_VIEW (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return SYSPROF_CAPTURE_VIEW_GET_CLASS (self)->load_finish (self, result, error);
}

/**
 * sysprof_capture_view_get_busy:
 * @self: a #SysprofCaptureView
 *
 * Returns: %TRUE if the view is busy loading capture contents
 *
 * Since: 3.34
 */
gboolean
sysprof_capture_view_get_busy (SysprofCaptureView *self)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CAPTURE_VIEW (self), FALSE);

  return priv->busy > 0;
}

void
sysprof_capture_view_reset (SysprofCaptureView *self)
{
  g_return_if_fail (SYSPROF_IS_CAPTURE_VIEW (self));

  /* TODO: reset */
  g_warning ("Clear all loaded state");
}

/**
 * sysprof_capture_view_get_zoom_manager:
 * @self: a #SysprofCaptureView
 *
 * Returns: (transfer none): a #SysprofZoomManager
 *
 * Since: 3.34
 */
SysprofZoomManager *
sysprof_capture_view_get_zoom_manager (SysprofCaptureView *self)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CAPTURE_VIEW (self), NULL);

  return priv->zoom_manager;
}

void
sysprof_capture_view_fit_to_width (SysprofCaptureView *self)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);
  GtkAllocation alloc;
  gdouble zoom;
  gint64 duration;
  gint width;

  g_return_if_fail (SYSPROF_IS_CAPTURE_VIEW (self));

  /* Trim a bit off the width to avoid drawing past edges */
  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);
  width = MAX (100, alloc.width - 25);

  duration = priv->features.end_time - priv->features.begin_time;
  zoom = sysprof_zoom_manager_fit_zoom_for_duration (priv->zoom_manager, duration, width);
  sysprof_zoom_manager_set_zoom (priv->zoom_manager, zoom);
}

/**
 * sysprof_capture_view_get_reader:
 *
 * Gets the reader for the view, if any.
 *
 * Returns: (transfer none): a #SysprofCaptureReader or %NULL
 *
 * Since: 3.34
 */
SysprofCaptureReader *
sysprof_capture_view_get_reader (SysprofCaptureView *self)
{
  SysprofCaptureViewPrivate *priv = sysprof_capture_view_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CAPTURE_VIEW (self), NULL);

  return priv->reader;
}
