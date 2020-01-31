/* sysprof-display.c
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

#define G_LOG_DOMAIN "sysprof-display"

#include "config.h"

#include <dazzle.h>
#include <glib/gi18n.h>

#include "sysprof-details-page.h"
#include "sysprof-display.h"
#include "sysprof-profiler-assistant.h"
#include "sysprof-failed-state-view.h"
#include "sysprof-recording-state-view.h"
#include "sysprof-theme-manager.h"
#include "sysprof-ui-private.h"
#include "sysprof-visualizers-frame.h"
#include "sysprof-visualizer-group-private.h"

#include "sysprof-battery-aid.h"
#include "sysprof-callgraph-aid.h"
#include "sysprof-counters-aid.h"
#include "sysprof-cpu-aid.h"
#include "sysprof-diskstat-aid.h"
#include "sysprof-logs-aid.h"
#include "sysprof-marks-aid.h"
#include "sysprof-memory-aid.h"
#include "sysprof-memprof-aid.h"
#include "sysprof-netdev-aid.h"
#include "sysprof-rapl-aid.h"

typedef enum
{
  SYSPROF_CAPTURE_FLAGS_CAN_REPLAY = 1 << 1,
} SysprofCaptureFlags;

typedef struct
{
  SysprofCaptureReader      *reader;
  SysprofCaptureCondition   *filter;
  GFile                     *file;
  SysprofProfiler           *profiler;
  GError                    *error;

  /* Template Widgets */
  SysprofVisualizersFrame   *visualizers;
  GtkStack                  *pages;
  SysprofDetailsPage        *details;
  GtkStack                  *stack;
  SysprofProfilerAssistant  *assistant;
  SysprofRecordingStateView *recording_view;
  SysprofFailedStateView    *failed_view;

  SysprofCaptureFlags        flags;
} SysprofDisplayPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofDisplay, sysprof_display, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_CAN_REPLAY,
  PROP_CAN_SAVE,
  PROP_RECORDING,
  PROP_TITLE,
  PROP_VISIBLE_PAGE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
update_title_child_property (SysprofDisplay *self)
{
  GtkWidget *parent;

  g_assert (SYSPROF_IS_DISPLAY (self));

  if ((parent = gtk_widget_get_parent (GTK_WIDGET (self))) && GTK_IS_NOTEBOOK (parent))
    {
      g_autofree gchar *title = sysprof_display_dup_title (self);

      gtk_container_child_set (GTK_CONTAINER (parent), GTK_WIDGET (self),
                               "menu-label", title,
                               NULL);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
sysprof_display_profiler_failed_cb (SysprofDisplay  *self,
                                    const GError    *error,
                                    SysprofProfiler *profiler)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (error != NULL);
  g_assert (SYSPROF_IS_PROFILER (profiler));

  g_clear_object (&priv->profiler);

  /* Save the error for future use */
  g_clear_error (&priv->error);
  priv->error = g_error_copy (error);

  gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->failed_view));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_RECORDING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
sysprof_display_profiler_stopped_cb (SysprofDisplay  *self,
                                     SysprofProfiler *profiler)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofCaptureWriter *writer;

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  if ((writer = sysprof_profiler_get_writer (profiler)))
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;
      g_autoptr(GError) error = NULL;

      if (!(reader = sysprof_capture_writer_create_reader (writer, &error)))
        {
          g_warning ("Failed to create capture creader: %s\n", error->message);
          gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->failed_view));
          goto notify;
        }

      sysprof_display_load_async (self,
                                  reader,
                                  NULL, NULL, NULL);
      gtk_stack_set_visible_child_name (priv->stack, "view");
    }

notify:
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLAY]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_SAVE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_RECORDING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
sysprof_display_set_profiler (SysprofDisplay  *self,
                              SysprofProfiler *profiler)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  if (g_set_object (&priv->profiler, profiler))
    {
      sysprof_recording_state_view_set_profiler (priv->recording_view, profiler);
      gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->recording_view));

      g_signal_connect_object (profiler,
                               "stopped",
                               G_CALLBACK (sysprof_display_profiler_stopped_cb),
                               self,
                               G_CONNECT_SWAPPED);

      g_signal_connect_object (profiler,
                               "failed",
                               G_CALLBACK (sysprof_display_profiler_failed_cb),
                               self,
                               G_CONNECT_SWAPPED);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_RECORDING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
sysprof_display_start_recording_cb (SysprofDisplay           *self,
                                    SysprofProfiler          *profiler,
                                    SysprofProfilerAssistant *assistant)
{
  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (!assistant || SYSPROF_IS_PROFILER_ASSISTANT (assistant));
  g_assert (sysprof_display_is_empty (self));

  sysprof_display_set_profiler (self, profiler);
  sysprof_profiler_start (profiler);
}

static gboolean
sysprof_display_get_is_recording (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (SYSPROF_IS_DISPLAY (self));

  return GTK_WIDGET (priv->recording_view) == gtk_stack_get_visible_child (priv->stack);
}

gchar *
sysprof_display_dup_title (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  if (priv->error)
    return g_strdup (_("Recording Failed"));

  if (priv->profiler != NULL)
    {
      if (sysprof_profiler_get_is_running (priv->profiler))
        return g_strdup (_("Recording…"));
    }

  if (priv->file != NULL)
    return g_file_get_basename (priv->file);

  if (priv->reader != NULL)
    {
      g_autoptr(GDateTime) dt = NULL;
      const gchar *filename;
      const gchar *capture_time;

      if ((filename = sysprof_capture_reader_get_filename (priv->reader)))
        return g_path_get_basename (filename);

      /* This recording is not yet on disk, but the time it was recorded
       * is much more useful than "New Recording".
       */
      capture_time = sysprof_capture_reader_get_time (priv->reader);

      if ((dt = g_date_time_new_from_iso8601 (capture_time, NULL)))
        {
          g_autofree gchar *formatted = g_date_time_format (dt, "%X");
          /* translators: %s is replaced with locale specific time of recording */
          return g_strdup_printf (_("Recording at %s"), formatted);
        }
    }

  return g_strdup (_("New Recording"));
}

/**
 * sysprof_display_new:
 *
 * Create a new #SysprofDisplay.
 *
 * Returns: (transfer full): a newly created #SysprofDisplay
 */
GtkWidget *
sysprof_display_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISPLAY, NULL);
}

static void
sysprof_display_notify_selection_cb (SysprofDisplay          *self,
                                     GParamSpec              *pspec,
                                     SysprofVisualizersFrame *visualizers)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofSelection *selection;

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (visualizers));

  g_clear_pointer (&priv->filter, sysprof_capture_condition_unref);

  if ((selection = sysprof_visualizers_frame_get_selection (visualizers)))
    {
      SysprofCaptureCondition *cond = NULL;
      guint n_ranges = sysprof_selection_get_n_ranges (selection);

      for (guint i = 0; i < n_ranges; i++)
        {
          SysprofCaptureCondition *c;
          gint64 begin, end;

          sysprof_selection_get_nth_range (selection, i, &begin, &end);
          c = sysprof_capture_condition_new_where_time_between (begin, end);

          if (cond == NULL)
            cond = c;
          else
            cond = sysprof_capture_condition_new_or (cond, c);
        }

      priv->filter = cond;

      /* Opportunistically load pages */
      if (priv->reader != NULL)
        {
          GList *pages = gtk_container_get_children (GTK_CONTAINER (priv->pages));

          for (const GList *iter = pages; iter; iter = iter->next)
            {
              if (SYSPROF_IS_PAGE (iter->data))
                sysprof_page_load_async (iter->data,
                                         priv->reader,
                                         selection,
                                         priv->filter,
                                         NULL, NULL, NULL);
            }

          g_list_free (pages);
        }
    }
}

static void
change_page_cb (GSimpleAction *action,
                GVariant      *param,
                gpointer       user_data)
{
  SysprofDisplay *self = user_data;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (param != NULL);

  if (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING))
    {
      const gchar *str = g_variant_get_string (param, NULL);

      gtk_stack_set_visible_child_name (priv->pages, str);

      if (g_str_equal (str, "details"))
        sysprof_visualizers_frame_unselect_row (priv->visualizers);
    }
}

static void
stop_recording_cb (GSimpleAction *action,
                   GVariant      *param,
                   gpointer       user_data)
{
  SysprofDisplay *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (SYSPROF_IS_DISPLAY (self));

  sysprof_display_stop_recording (self);
}

static void
sysprof_display_finalize (GObject *object)
{
  SysprofDisplay *self = (SysprofDisplay *)object;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&priv->filter, sysprof_capture_condition_unref);

  G_OBJECT_CLASS (sysprof_display_parent_class)->finalize (object);
}

static void
sysprof_display_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofDisplay *self = SYSPROF_DISPLAY (object);

  switch (prop_id)
    {
    case PROP_CAN_REPLAY:
      g_value_set_boolean (value, sysprof_display_get_can_replay (self));
      break;

    case PROP_CAN_SAVE:
      g_value_set_boolean (value, sysprof_display_get_can_save (self));
      break;

    case PROP_RECORDING:
      g_value_set_boolean (value, sysprof_display_get_is_recording (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, sysprof_display_dup_title (self));
      break;

    case PROP_VISIBLE_PAGE:
      g_value_set_object (value, sysprof_display_get_visible_page (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_display_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofDisplay *self = SYSPROF_DISPLAY (object);

  switch (prop_id)
    {
    case PROP_VISIBLE_PAGE:
      sysprof_display_set_visible_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_display_class_init (SysprofDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_display_finalize;
  object_class->get_property = sysprof_display_get_property;
  object_class->set_property = sysprof_display_set_property;

  sysprof_theme_manager_register_resource (sysprof_theme_manager_get_default (),
                                           NULL,
                                           NULL,
                                           "/org/gnome/sysprof/css/SysprofDisplay-shared.css");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-display.ui");
  gtk_widget_class_set_css_name (widget_class, "SysprofDisplay");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, assistant);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, details);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, failed_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, pages);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, recording_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, stack);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, visualizers);

  properties [PROP_CAN_REPLAY] =
    g_param_spec_boolean ("can-replay",
                          "Can Replay",
                          "If the capture contains enough information to re-run the recording",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_SAVE] =
    g_param_spec_boolean ("can-save",
                          "Can Save",
                          "If the display can save a recording",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_RECORDING] =
    g_param_spec_boolean ("recording",
                          "Recording",
                          "If the display is in recording state",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the display",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_VISIBLE_PAGE] =
    g_param_spec_object ("visible-page",
                         "Visible Page",
                         "Visible Page",
                         SYSPROF_TYPE_PAGE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (DZL_TYPE_MULTI_PANED);
  g_type_ensure (SYSPROF_TYPE_DETAILS_PAGE);
  g_type_ensure (SYSPROF_TYPE_FAILED_STATE_VIEW);
  g_type_ensure (SYSPROF_TYPE_PROFILER_ASSISTANT);
  g_type_ensure (SYSPROF_TYPE_RECORDING_STATE_VIEW);
  g_type_ensure (SYSPROF_TYPE_VISUALIZERS_FRAME);
}

static void
sysprof_display_init (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
  static GActionEntry entries[] = {
    { "page", change_page_cb, "s" },
    { "stop-recording", stop_recording_cb },
  };
  g_autoptr(GPropertyAction) page = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (priv->assistant,
                           "start-recording",
                           G_CALLBACK (sysprof_display_start_recording_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->visualizers,
                           "notify::selection",
                           G_CALLBACK (sysprof_display_notify_selection_cb),
                           self,
                           G_CONNECT_SWAPPED);

  page = g_property_action_new ("page", priv->pages, "visible-child-name");
  g_action_map_add_action_entries (G_ACTION_MAP (group),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "display", G_ACTION_GROUP (group));
}

void
sysprof_display_add_group (SysprofDisplay         *self,
                           SysprofVisualizerGroup *group)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (group));

  if (priv->reader != NULL)
    _sysprof_visualizer_group_set_reader (group, priv->reader);

  gtk_container_add (GTK_CONTAINER (priv->visualizers), GTK_WIDGET (group));
}

void
sysprof_display_add_page (SysprofDisplay *self,
                          SysprofPage    *page)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofSelection *selection;
  const gchar *title;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (SYSPROF_IS_PAGE (page));

  title = sysprof_page_get_title (page);

  gtk_container_add_with_properties (GTK_CONTAINER (priv->pages), GTK_WIDGET (page),
                                     "title", title,
                                     NULL);

  selection = sysprof_visualizers_frame_get_selection (priv->visualizers);

  sysprof_page_set_size_group (page,
                               sysprof_visualizers_frame_get_size_group (priv->visualizers));

  sysprof_page_set_hadjustment (page,
                                sysprof_visualizers_frame_get_hadjustment (priv->visualizers));

  if (priv->reader != NULL)
    sysprof_page_load_async (page,
                             priv->reader,
                             selection,
                             priv->filter,
                             NULL, NULL, NULL);
}

SysprofPage *
sysprof_display_get_visible_page (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  GtkWidget *visible_page;

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  visible_page = gtk_stack_get_visible_child (priv->pages);

  if (SYSPROF_IS_PAGE (visible_page))
    return SYSPROF_PAGE (visible_page);

  return NULL;
}

void
sysprof_display_set_visible_page (SysprofDisplay *self,
                                  SysprofPage    *page)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (SYSPROF_IS_PAGE (page));

  gtk_stack_set_visible_child (priv->pages, GTK_WIDGET (page));
}

static void
sysprof_display_present_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  SysprofAid *aid = (SysprofAid *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  guint *n_active;

  g_assert (SYSPROF_IS_AID (aid));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!sysprof_aid_present_finish (aid, result, &error))
    g_warning ("Failed to present aid %s: %s", G_OBJECT_TYPE_NAME (aid), error->message);

  n_active = g_task_get_task_data (task);

  (*n_active)--;

  if (n_active == 0)
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_display_present_async (SysprofDisplay       *self,
                               SysprofCaptureReader *reader,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  g_autoptr(GPtrArray) aids = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  aids = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (aids, sysprof_battery_aid_new ());
  g_ptr_array_add (aids, sysprof_counters_aid_new ());
  g_ptr_array_add (aids, sysprof_cpu_aid_new ());
  g_ptr_array_add (aids, sysprof_callgraph_aid_new ());
  g_ptr_array_add (aids, sysprof_diskstat_aid_new ());
  g_ptr_array_add (aids, sysprof_logs_aid_new ());
  g_ptr_array_add (aids, sysprof_marks_aid_new ());
  g_ptr_array_add (aids, sysprof_memory_aid_new ());
  g_ptr_array_add (aids, sysprof_memprof_aid_new ());
  g_ptr_array_add (aids, sysprof_netdev_aid_new ());
  g_ptr_array_add (aids, sysprof_rapl_aid_new ());

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_display_present_async);

  if (aids->len == 0)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  g_task_set_task_data (task, g_memdup (&aids->len, sizeof aids->len), g_free);

  for (guint i = 0; i < aids->len; i++)
    {
      SysprofAid *aid = g_ptr_array_index (aids, i);

      sysprof_aid_present_async (aid,
                                 reader,
                                 self,
                                 cancellable,
                                 sysprof_display_present_cb,
                                 g_object_ref (task));
    }
}

static gboolean
sysprof_display_present_finish (SysprofDisplay  *self,
                                GAsyncResult    *result,
                                GError         **error)
{
  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_display_scan_worker (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
  SysprofDisplay *self = source_object;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofCaptureReader *reader = task_data;
  g_autoptr(GHashTable) mark_stats = NULL;
  g_autoptr(GArray) marks = NULL;
  SysprofCaptureFrame frame;
  SysprofCaptureStat st = {{0}};
  SysprofCaptureFlags flags = 0;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /* Scan the reader until the end so that we know we have gotten
   * all of the timing data loaded into the underlying reader.
   */

  mark_stats = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  marks = g_array_new (FALSE, FALSE, sizeof (SysprofMarkStat));

  while (sysprof_capture_reader_peek_frame (reader, &frame))
    {
      st.frame_count[frame.type]++;

      if (frame.type == SYSPROF_CAPTURE_FRAME_METADATA)
        {
          const SysprofCaptureMetadata *meta;

          if ((meta = sysprof_capture_reader_read_metadata (reader)))
            {
              if (g_strcmp0 (meta->id, "local-profiler") == 0)
                flags |= SYSPROF_CAPTURE_FLAGS_CAN_REPLAY;
            }
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_MARK)
        {
          const SysprofCaptureMark *mark;

          if ((mark = sysprof_capture_reader_read_mark (reader)))
            {
              SysprofMarkStat *mstat;
              gchar name[152];
              gpointer idx;

              g_snprintf (name, sizeof name, "%s:%s", mark->group, mark->name);

              if (!(idx = g_hash_table_lookup (mark_stats, name)))
                {
                  SysprofMarkStat empty = {{0}};

                  g_strlcpy (empty.name, name, sizeof empty.name);
                  g_array_append_val (marks, empty);
                  idx = GUINT_TO_POINTER (marks->len);
                  g_hash_table_insert (mark_stats, g_strdup (name), idx);
                }

              mstat = &g_array_index (marks, SysprofMarkStat, GPOINTER_TO_UINT (idx) - 1);

              if (mark->duration > 0)
                {
                  if (mstat->min == 0 || mark->duration < mstat->min)
                    mstat->min = mark->duration;
                }

              if (mark->duration > mstat->max)
                mstat->max = mark->duration;

              if (mark->duration > 0)
                {
                  mstat->avg += mark->duration;
                  mstat->avg_count++;
                }

              mstat->count++;
            }
        }
      else
        {
          sysprof_capture_reader_skip (reader);
        }
    }

  {
    GHashTableIter iter;
    gpointer k,v;

    g_hash_table_iter_init (&iter, mark_stats);
    while (g_hash_table_iter_next (&iter, &k, &v))
      {
        guint idx = GPOINTER_TO_UINT (v) - 1;
        SysprofMarkStat *mstat = &g_array_index (marks, SysprofMarkStat, idx);

        if (mstat->avg_count > 0 && mstat->avg > 0)
          mstat->avg /= mstat->avg_count;

#if 0
        g_print ("%s: count=%ld avg=%ld min=%ld max=%ld\n",
                 (gchar*)k,
                 ((SysprofMarkStat *)v)->count,
                 ((SysprofMarkStat *)v)->avg,
                 ((SysprofMarkStat *)v)->min,
                 ((SysprofMarkStat *)v)->max);
#endif
      }
  }

  g_object_set_data_full (G_OBJECT (task),
                          "MARK_STAT",
                          g_steal_pointer (&marks),
                          (GDestroyNotify) g_array_unref);

  g_atomic_int_set (&priv->flags, flags);
  sysprof_capture_reader_reset (reader);
  sysprof_capture_reader_set_stat (reader, &st);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_display_scan_async (SysprofDisplay       *self,
                            SysprofCaptureReader *reader,
                            GCancellable         *cancellable,
                            GAsyncReadyCallback   callback,
                            gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_display_scan_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_ref (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);
  g_task_run_in_thread (task, sysprof_display_scan_worker);
}

static gboolean
sysprof_display_scan_finish (SysprofDisplay  *self,
                             GAsyncResult    *result,
                             GError         **error)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  GArray *marks;

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if ((marks = g_object_get_data (G_OBJECT (result), "MARK_STAT")))
    sysprof_details_page_add_marks (priv->details,
                                    (const SysprofMarkStat *)(gpointer)marks->data,
                                    marks->len);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_display_load_present_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  SysprofDisplay *self = (SysprofDisplay *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!sysprof_display_present_finish (self, result, &error))
    g_warning ("Error presenting: %s", error->message);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_display_load_frame_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  SysprofVisualizersFrame *frame = (SysprofVisualizersFrame *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofCaptureReader *reader;
  SysprofDisplay *self;
  GCancellable *cancellable;

  g_assert (SYSPROF_IS_VISUALIZERS_FRAME (frame));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  reader = g_task_get_task_data (task);
  cancellable = g_task_get_cancellable (task);

  if (!sysprof_visualizers_frame_load_finish (frame, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    sysprof_display_present_async (self,
                                   reader,
                                   cancellable,
                                   sysprof_display_load_present_cb,
                                   g_steal_pointer (&task));
}

static void
sysprof_display_load_scan_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  SysprofDisplay *self = (SysprofDisplay *)object;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  SysprofCaptureReader *reader;
  SysprofSelection *selection;
  GCancellable *cancellable;
  GList *pages;

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  reader = g_task_get_task_data (task);
  cancellable = g_task_get_cancellable (task);

  if (!sysprof_display_scan_finish (self, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    sysprof_visualizers_frame_load_async (priv->visualizers,
                                          reader,
                                          cancellable,
                                          sysprof_display_load_frame_cb,
                                          g_steal_pointer (&task));

  selection = sysprof_visualizers_frame_get_selection (priv->visualizers);

  sysprof_details_page_set_reader (priv->details, reader);

  /* Opportunistically load pages */
  pages = gtk_container_get_children (GTK_CONTAINER (priv->pages));
  for (const GList *iter = pages; iter; iter = iter->next)
    {
      if (SYSPROF_IS_PAGE (iter->data))
        sysprof_page_load_async (iter->data,
                                 reader,
                                 selection,
                                 priv->filter,
                                 NULL, NULL, NULL);
    }
  g_list_free (pages);

  gtk_stack_set_visible_child_name (priv->stack, "view");
}

void
sysprof_display_load_async (SysprofDisplay       *self,
                            SysprofCaptureReader *reader,
                            GCancellable         *cancellable,
                            GAsyncReadyCallback   callback,
                            gpointer              user_data)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (priv->reader != reader)
    {
      g_clear_pointer (&priv->reader, sysprof_capture_reader_unref);
      priv->reader = sysprof_capture_reader_ref (reader);
    }

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_display_load_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_ref (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);

  /* First scan the reader for any sort of data we care about before
   * we notify aids to load content. That allows us to ensure we have
   * proper timing data for the consumers.
   */
  sysprof_display_scan_async (self,
                              reader,
                              cancellable,
                              sysprof_display_load_scan_cb,
                              g_steal_pointer (&task));
}

gboolean
sysprof_display_load_finish (SysprofDisplay  *self,
                             GAsyncResult    *result,
                             GError         **error)
{
  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

SysprofZoomManager *
sysprof_display_get_zoom_manager (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  return sysprof_visualizers_frame_get_zoom_manager (priv->visualizers);
}

/**
 * sysprof_display_is_empty:
 *
 * Checks if any content is or will be loaded into @self.
 *
 * Returns: %TRUE if the tab is unperterbed.
 *
 * Since: 3.34
 */
gboolean
sysprof_display_is_empty (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);

  return priv->file == NULL &&
         priv->profiler == NULL &&
         gtk_stack_get_visible_child (priv->stack) == GTK_WIDGET (priv->assistant) &&
         NULL == priv->reader;
}

void
sysprof_display_open (SysprofDisplay *self,
                      GFile          *file)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *path = NULL;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (g_file_is_native (file));
  g_return_if_fail (sysprof_display_is_empty (self));

  path = g_file_get_path (file);

  /* If the file is executable, just set the path to the binary
   * in the profiler assistant.
   */
  if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
    {
      sysprof_profiler_assistant_set_executable (priv->assistant, path);
      return;
    }

  g_set_object (&priv->file, file);

  if (!(reader = sysprof_capture_reader_new (path, &error)))
    g_warning ("Failed to open capture: %s", error->message);
  else
    sysprof_display_load_async (self, reader, NULL, NULL, NULL);

  update_title_child_property (self);
}

gboolean
sysprof_display_get_can_save (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);

  return priv->reader != NULL;
}

void
sysprof_display_stop_recording (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));

  if (priv->profiler != NULL)
    sysprof_profiler_stop (priv->profiler);
}

void
_sysprof_display_focus_record (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));

  _sysprof_profiler_assistant_focus_record (priv->assistant);
}

gboolean
sysprof_display_get_can_replay (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);

  return !sysprof_display_is_empty (self) &&
          priv->reader != NULL &&
          !!(priv->flags & SYSPROF_CAPTURE_FLAGS_CAN_REPLAY);
}

/**
 * sysprof_display_replay:
 * @self: a #SysprofDisplay
 *
 * Gets a new display that will re-run the previous recording that is being
 * viewed. This requires a capture file which contains enough information to
 * replay the operation.
 *
 * Returns: (nullable) (transfer full): a #SysprofDisplay or %NULL
 *
 * Since: 3.34
 */
SysprofDisplay *
sysprof_display_replay (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(SysprofProfiler) profiler = NULL;
  SysprofDisplay *copy;

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);
  g_return_val_if_fail (priv->reader != NULL, NULL);

  profiler = sysprof_local_profiler_new_replay (priv->reader);
  g_return_val_if_fail (profiler != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_LOCAL_PROFILER (profiler), NULL);

  copy = g_object_new (SYSPROF_TYPE_DISPLAY, NULL);
  sysprof_display_set_profiler (copy, profiler);
  sysprof_profiler_start (profiler);

  return g_steal_pointer (&copy);
}

GtkWidget *
sysprof_display_new_for_profiler (SysprofProfiler *profiler)
{
  SysprofDisplay *self;

  g_return_val_if_fail (SYSPROF_IS_PROFILER (profiler), NULL);

  self = g_object_new (SYSPROF_TYPE_DISPLAY, NULL);
  sysprof_display_set_profiler (self, profiler);

  return GTK_WIDGET (g_steal_pointer (&self));
}

void
sysprof_display_save (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GFile) file = NULL;
  GtkFileChooserNative *native;
  GtkWindow *parent;
  gint res;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (priv->reader != NULL);

  parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self)));

  native = gtk_file_chooser_native_new (_("Save Recording"),
                                        parent,
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Save"),
                                        _("Cancel"));
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (native), "capture.syscap");

  res = gtk_native_dialog_run (GTK_NATIVE_DIALOG (native));

  switch (res)
    {
    case GTK_RESPONSE_ACCEPT:
      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));

      if (g_file_is_native (file))
        {
          g_autofree gchar *path = g_file_get_path (file);
          g_autoptr(GError) error = NULL;

          if (!sysprof_capture_reader_save_as (priv->reader, path, &error))
            {
              GtkWidget *msg;

              msg = gtk_message_dialog_new (parent,
                                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE,
                                            _("Failed to save recording: %s"),
                                            error->message);
              gtk_window_present (GTK_WINDOW (msg));
              g_signal_connect (msg, "response", G_CALLBACK (gtk_widget_destroy), NULL);
            }
        }

      break;

    default:
      break;
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}
