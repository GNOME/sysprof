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

#include <glib/gi18n.h>

#include <sysprof-capture.h>
#include <sysprof-ui.h>
#include <sysprof.h>

#include "sysprof-profiler-assistant.h"
#include "sysprof-capture-view.h"
#include "sysprof-display.h"
#include "sysprof-empty-state-view.h"
#include "sysprof-recording-state-view.h"

typedef struct
{
  GFile                     *file;
  SysprofProfiler           *profiler;
  GError                    *error;

  /* Template Objects */
  SysprofProfilerAssistant  *assistant;
  SysprofCaptureView        *capture_view;
  SysprofEmptyStateView     *failed_view;
  SysprofRecordingStateView *recording_view;
  GtkStack                  *stack;
} SysprofDisplayPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofDisplay, sysprof_display, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_CAN_SAVE,
  PROP_RECORDING,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_display_new:
 *
 * Create a new #SysprofDisplay.
 *
 * Returns: (transfer full): a newly created #SysprofDisplay
 *
 * Since: 3.34
 */
GtkWidget *
sysprof_display_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISPLAY, NULL);
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

      sysprof_capture_view_load_async (priv->capture_view, reader, NULL, NULL, NULL);
      gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->capture_view));
    }

notify:
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_SAVE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_RECORDING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
sysprof_display_start_recording_cb (SysprofDisplay           *self,
                                    SysprofProfiler          *profiler,
                                    SysprofProfilerAssistant *assistant)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (assistant));
  g_assert (sysprof_display_is_empty (self));

  if (g_set_object (&priv->profiler, profiler))
    {
      sysprof_recording_state_view_set_profiler (priv->recording_view, profiler);
      gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->recording_view));

      g_signal_connect_object (priv->profiler,
                               "stopped",
                               G_CALLBACK (sysprof_display_profiler_stopped_cb),
                               self,
                               G_CONNECT_SWAPPED);

      g_signal_connect_object (priv->profiler,
                               "failed",
                               G_CALLBACK (sysprof_display_profiler_failed_cb),
                               self,
                               G_CONNECT_SWAPPED);

      sysprof_profiler_start (priv->profiler);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_RECORDING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

gchar *
sysprof_display_dup_title (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofCaptureReader *reader;

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  if (priv->error)
    return g_strdup (_("Failed Recording"));

  if (priv->profiler != NULL)
    {
      if (sysprof_profiler_get_is_running (priv->profiler))
        return g_strdup (_("Recording…"));
    }

  if (priv->file != NULL)
    return g_file_get_basename (priv->file);

  if ((reader = sysprof_capture_view_get_reader (priv->capture_view)))
    {
      const gchar *filename;

      if ((filename = sysprof_capture_reader_get_filename (reader)))
        return g_strdup (filename);
    }

  return g_strdup (_("New Recording"));
}

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

static gboolean
sysprof_display_get_is_recording (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (SYSPROF_IS_DISPLAY (self));

  return GTK_WIDGET (priv->recording_view) == gtk_stack_get_visible_child (priv->stack);
}

static void
stop_recording_cb (GSimpleAction *action,
                   GVariant      *param,
                   gpointer       user_data)
{
  SysprofDisplay *self = user_data;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (SYSPROF_IS_DISPLAY (self));

  if (priv->profiler != NULL)
    sysprof_profiler_stop (priv->profiler);
}

static void
sysprof_display_parent_set (GtkWidget *widget,
                            GtkWidget *old_parent)
{
  g_assert (SYSPROF_IS_DISPLAY (widget));

  if (GTK_WIDGET_CLASS (sysprof_display_parent_class)->parent_set)
    GTK_WIDGET_CLASS (sysprof_display_parent_class)->parent_set (widget, old_parent);

  update_title_child_property (SYSPROF_DISPLAY (widget));
}

static void
sysprof_display_finalize (GObject *object)
{
  SysprofDisplay *self = (SysprofDisplay *)object;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_clear_error (&priv->error);
  g_clear_object (&priv->profiler);
  g_clear_object (&priv->file);

  G_OBJECT_CLASS (sysprof_display_parent_class)->finalize (object);
}

static void
sysprof_display_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofDisplay *self = (SysprofDisplay *)object;

  switch (prop_id)
    {
    case PROP_CAN_SAVE:
      g_value_set_boolean (value, sysprof_display_get_can_save (self));
      break;

    case PROP_RECORDING:
      g_value_set_boolean (value, sysprof_display_get_is_recording (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, sysprof_display_dup_title (self));
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
  switch (prop_id)
    {
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

  widget_class->parent_set = sysprof_display_parent_set;

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
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-display.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, capture_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, failed_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, recording_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, assistant);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay, stack);

  g_type_ensure (SYSPROF_TYPE_CAPTURE_VIEW);
  g_type_ensure (SYSPROF_TYPE_EMPTY_STATE_VIEW);
  g_type_ensure (SYSPROF_TYPE_RECORDING_STATE_VIEW);
}

static void
sysprof_display_init (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
  static const GActionEntry actions[] = {
    { "stop-recording", stop_recording_cb },
  };

  gtk_widget_init_template (GTK_WIDGET (self));

  g_action_map_add_action_entries (G_ACTION_MAP (group),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "display", G_ACTION_GROUP (group));

  g_signal_connect_object (priv->assistant,
                           "start-recording",
                           G_CALLBACK (sysprof_display_start_recording_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

/**
 * sysprof_display_get_profiler:
 *
 * Gets the proflier for the display.
 *
 * Returns: (transfer none) (nullable): a #SysprofProfiler or %NULL
 *
 * Since: 3.34
 */
SysprofProfiler *
sysprof_display_get_profiler (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  return priv->profiler;
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
         NULL == sysprof_capture_view_get_reader (priv->capture_view);
}

static void
sysprof_display_open_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(SysprofDisplay) self = user_data;
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DISPLAY (self));
  g_assert (G_IS_TASK (result));

  if (!(reader = g_task_propagate_pointer (G_TASK (result), &error)))
    {
      gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->failed_view));
      return;
    }

  sysprof_capture_view_load_async (priv->capture_view, reader, NULL, NULL, NULL);
  gtk_stack_set_visible_child (priv->stack, GTK_WIDGET (priv->capture_view));
}

static void
sysprof_display_open_worker (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
  g_autofree gchar *path = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureReader *reader;
  GFile *file = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (source_object == NULL);
  g_assert (G_IS_FILE (file));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  path = g_file_get_path (file);

  if (!(reader = sysprof_capture_reader_new (path, &error)))
    g_task_return_error (task, g_steal_pointer ((&error)));
  else
    g_task_return_pointer (task,
                           g_steal_pointer (&reader),
                           (GDestroyNotify) sysprof_capture_reader_unref);
}

void
sysprof_display_open (SysprofDisplay *self,
                      GFile          *file)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (g_file_is_native (file));
  g_return_if_fail (sysprof_display_is_empty (self));

  g_set_object (&priv->file, file);

  task = g_task_new (NULL, NULL, sysprof_display_open_cb, g_object_ref (self));
  g_task_set_task_data (task, g_file_dup (file), g_object_unref);
  g_task_run_in_thread (task, sysprof_display_open_worker);

  update_title_child_property (self);
}

void
sysprof_display_save (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  g_autoptr(GFile) file = NULL;
  GtkFileChooserNative *native;
  SysprofCaptureReader *reader;
  GtkWindow *parent;
  gint res;

  g_return_if_fail (SYSPROF_IS_DISPLAY (self));

  if (!(reader = sysprof_capture_view_get_reader (priv->capture_view)))
    return;

  parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self)));

  native = gtk_file_chooser_native_new (_("Save Recording"),
                                        parent,
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Save"),
                                        _("Cancel"));
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER (native), TRUE);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (native), "capture.sysprof");

  res = gtk_native_dialog_run (GTK_NATIVE_DIALOG (native));

  switch (res)
    {
    case GTK_RESPONSE_ACCEPT:
      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));

      if (g_file_is_native (file))
        {
          g_autofree gchar *path = g_file_get_path (file);
          g_autoptr(GError) error = NULL;

          if (!sysprof_capture_reader_save_as (reader, path, &error))
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

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

gboolean
sysprof_display_get_can_save (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), FALSE);

  return sysprof_capture_view_get_reader (priv->capture_view) != NULL;
}
