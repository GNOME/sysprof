/* sp-window.c
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

#include <errno.h>
#include <glib/gi18n.h>
#include <sysprof.h>
#include <sysprof-ui.h>

#include "sp-application.h"
#include "sp-window.h"
#include "sp-window-settings.h"

struct _SpWindow
{
  GtkApplicationWindow  parent_instance;

  SpWindowState         state;

  SpProfiler           *profiler;
  SpCaptureReader      *reader;

  /* Gtk widget template children */
  SpCallgraphView      *callgraph_view;
  SpEmptyStateView     *empty_view;
  GtkMenuButton        *gear_menu_button;
  GtkInfoBar           *info_bar;
  GtkLabel             *info_bar_label;
  GtkRevealer          *info_bar_revealer;
  SpProfilerMenuButton *profiler_menu_button;
  SpRecordingStateView *recording_view;
  GtkButton            *record_button;
  GtkLabel             *subtitle;
  GtkLabel             *stat_label;
  GtkLabel             *title;
  GtkStack             *view_stack;

  guint                 stats_handler;
};

G_DEFINE_TYPE (SpWindow, sp_window, GTK_TYPE_APPLICATION_WINDOW)

static void sp_window_set_state (SpWindow      *self,
                                 SpWindowState  state);

enum {
  START_RECORDING,
  STOP_RECORDING,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void sp_window_set_profiler (SpWindow   *self,
                                    SpProfiler *profiler);

static void
sp_window_notify_user (SpWindow       *self,
                       GtkMessageType  message_type,
                       const gchar    *format,
                       ...)
{
  g_autofree gchar *str = NULL;
  va_list args;

  g_assert (SP_IS_WINDOW (self));
  g_assert (format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  gtk_info_bar_set_message_type (self->info_bar, message_type);
  gtk_label_set_label (self->info_bar_label, str);
  gtk_revealer_set_reveal_child (self->info_bar_revealer, TRUE);
}

static void
sp_window_action_set (SpWindow    *self,
                      const gchar *action_name,
                      const gchar *first_property,
                      ...)
{
  gpointer action;
  va_list args;

  g_assert (SP_IS_WINDOW (self));
  g_assert (action_name != NULL);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), action_name);

  if (action == NULL)
    {
      g_warning ("Failed to locate action \"%s\"", action_name);
      return;
    }

  va_start (args, first_property);
  g_object_set_valist (action, first_property, args);
  va_end (args);
}

static gboolean
sp_window_update_stats (gpointer data)
{
  SpWindow *self = data;

  g_assert (SP_IS_WINDOW (self));

  if (self->profiler != NULL)
    {
      SpCaptureWriter *writer;

      if (NULL != (writer = sp_profiler_get_writer (self->profiler)))
        {
          g_autofree gchar *str = NULL;
          SpCaptureStat stbuf;
          guint count;

          sp_capture_writer_stat (writer, &stbuf);

          count = stbuf.frame_count[SP_CAPTURE_FRAME_SAMPLE];
          str = g_strdup_printf (_("Samples: %u"), count);
          gtk_label_set_label (self->stat_label, str);
        }
    }

  return G_SOURCE_CONTINUE;
}


static void
sp_window_update_subtitle (SpWindow *self)
{
  g_autofree gchar *relative = NULL;
  const gchar *filename;
  const gchar *date;
  GTimeVal tv;

  g_assert (SP_IS_WINDOW (self));
  g_assert (self->reader != NULL);

  if (NULL != (filename = sp_capture_reader_get_filename (self->reader)))
    {
      g_autoptr(GFile) home = NULL;
      g_autoptr(GFile) file = NULL;

      file = g_file_new_for_path (filename);
      home = g_file_new_for_path (g_get_home_dir ());

      if (g_file_has_prefix (file, home))
        filename = relative = g_file_get_relative_path (home, file);
    }

  if (filename == NULL)
    filename = _("[Memory Capture]");

  date = sp_capture_reader_get_time (self->reader);

  if (g_time_val_from_iso8601 (date, &tv))
    {
      g_autoptr(GDateTime) dt = NULL;
      g_autofree gchar *str = NULL;
      g_autofree gchar *label = NULL;

      dt = g_date_time_new_from_timeval_local (&tv);
      str = g_date_time_format (dt, "%x %X");

      label = g_strdup_printf (_("%s - %s"), filename, str);

      gtk_label_set_label (self->subtitle, label);
    }
  else
    gtk_label_set_label (self->subtitle, filename);
}

static void
sp_window_build_profile_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  SpProfile *profile = (SpProfile *)object;
  g_autoptr(SpWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SP_IS_CALLGRAPH_PROFILE (profile));
  g_assert (SP_IS_WINDOW (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!sp_profile_generate_finish (profile, result, &error))
    {
      sp_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", error->message);
      sp_window_set_state (self, SP_WINDOW_STATE_EMPTY);
      return;
    }

  sp_callgraph_view_set_profile (self->callgraph_view, SP_CALLGRAPH_PROFILE (profile));
  sp_window_set_state (self, SP_WINDOW_STATE_BROWSING);
}

static void
sp_window_build_profile (SpWindow *self)
{
  g_autoptr(SpProfile) profile = NULL;

  g_assert (SP_IS_WINDOW (self));
  g_assert (self->reader != NULL);

  profile = sp_callgraph_profile_new ();
  sp_profile_set_reader (profile, self->reader);
  sp_profile_generate (profile,
                       NULL,
                       sp_window_build_profile_cb,
                       g_object_ref (self));
}

static void
add_class (gpointer     widget,
           const gchar *name)
{
  g_assert (GTK_IS_WIDGET (widget));

  gtk_style_context_add_class (gtk_widget_get_style_context (widget), name);
}

static void
remove_class (gpointer     widget,
              const gchar *name)
{
  g_assert (GTK_IS_WIDGET (widget));

  gtk_style_context_remove_class (gtk_widget_get_style_context (widget), name);
}

static void
sp_window_set_state (SpWindow      *self,
                     SpWindowState  state)
{
  g_autoptr(SpProfiler) profiler = NULL;

  g_assert (SP_IS_WINDOW (self));

  if (self->state == state)
    return;

  self->state = state;

  switch (state)
    {
    case SP_WINDOW_STATE_EMPTY:
    case SP_WINDOW_STATE_FAILED:
      profiler = sp_local_profiler_new ();

      gtk_button_set_label (self->record_button, _("Record"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      add_class (self->record_button, "suggsted-action");
      remove_class (self->record_button, "destructive-action");
      if (state == SP_WINDOW_STATE_FAILED)
        gtk_stack_set_visible_child_name (self->view_stack, "failed");
      else
        gtk_stack_set_visible_child_name (self->view_stack, "empty");
      gtk_label_set_label (self->subtitle, _("Not running"));
      sp_callgraph_view_set_profile (self->callgraph_view, NULL);
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), FALSE);
      g_clear_pointer (&self->reader, sp_capture_reader_unref);
      sp_window_set_profiler (self, profiler);
      sp_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      break;

    case SP_WINDOW_STATE_RECORDING:
      gtk_button_set_label (self->record_button, _("Stop"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      remove_class (self->record_button, "suggsted-action");
      add_class (self->record_button, "destructive-action");
      gtk_stack_set_visible_child_name (self->view_stack, "recording");
      gtk_label_set_label (self->subtitle, _("Recording…"));
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), TRUE);
      g_clear_pointer (&self->reader, sp_capture_reader_unref);
      sp_callgraph_view_set_profile (self->callgraph_view, NULL);
      sp_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      break;

    case SP_WINDOW_STATE_PROCESSING:
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), FALSE);
      gtk_label_set_label (self->subtitle, _("Building profile…"));
      sp_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sp_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      sp_window_build_profile (self);
      break;

    case SP_WINDOW_STATE_BROWSING:
      gtk_button_set_label (self->record_button, _("Record"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      add_class (self->record_button, "suggsted-action");
      remove_class (self->record_button, "destructive-action");
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), TRUE);
      gtk_stack_set_visible_child_name (self->view_stack, "browsing");
      sp_window_update_stats (self);
      sp_window_update_subtitle (self);
      sp_window_action_set (self, "close-capture", "enabled", TRUE, NULL);
      sp_window_action_set (self, "save-capture", "enabled", TRUE, NULL);
      sp_window_action_set (self, "screenshot", "enabled", TRUE, NULL);

      break;

    default:
      g_warning ("Unknown state: %0d", state);
      break;
    }
}

static void
sp_window_enable_stats (SpWindow *self)
{
  g_assert (SP_IS_WINDOW (self));

  if (self->stats_handler == 0)
    self->stats_handler =
      g_timeout_add_seconds (1, sp_window_update_stats, self);
}

static void
sp_window_disable_stats (SpWindow *self)
{
  g_assert (SP_IS_WINDOW (self));

  if (self->stats_handler != 0)
    {
      g_source_remove (self->stats_handler);
      self->stats_handler = 0;
    }
}

static void
sp_window_add_sources (SpWindow   *window,
                       SpProfiler *profiler)
{
  g_autoptr(SpSource) proc_source = NULL;
  g_autoptr(SpSource) perf_source = NULL;

  g_assert (SP_IS_WINDOW (window));
  g_assert (SP_IS_PROFILER (profiler));

  proc_source = sp_proc_source_new ();
  sp_profiler_add_source (profiler, proc_source);

  perf_source = sp_perf_source_new ();
  sp_profiler_add_source (profiler, perf_source);
}

static void
sp_window_start_recording (SpWindow *self)
{
  g_assert (SP_IS_WINDOW (self));

  if (self->state == SP_WINDOW_STATE_RECORDING)
    {
      /* SpProfiler::stopped will move us to generating */
      gtk_label_set_label (self->subtitle, _("Stopping…"));
      sp_profiler_stop (self->profiler);
      return;
    }

  if ((self->state == SP_WINDOW_STATE_EMPTY) ||
      (self->state == SP_WINDOW_STATE_FAILED) ||
      (self->state == SP_WINDOW_STATE_BROWSING))
    {
      sp_window_add_sources (self, self->profiler);
      sp_window_set_state (self, SP_WINDOW_STATE_RECORDING);
      sp_window_enable_stats (self);
      sp_profiler_start (self->profiler);
      return;
    }
}

static void
sp_window_stop_recording (SpWindow *self)
{
  g_assert (SP_IS_WINDOW (self));

  if (self->state == SP_WINDOW_STATE_RECORDING)
    {
      if (self->profiler != NULL)
        sp_profiler_stop (self->profiler);
    }
}

static void
sp_window_hide_info_bar_revealer (SpWindow *self)
{
  g_assert (SP_IS_WINDOW (self));

  gtk_revealer_set_reveal_child (self->info_bar_revealer, FALSE);
}

static void
sp_window_profiler_stopped (SpWindow   *self,
                            SpProfiler *profiler)
{
  g_autoptr(SpCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;
  SpCaptureWriter *writer;

  g_assert (SP_IS_WINDOW (self));
  g_assert (SP_IS_PROFILER (profiler));

  sp_window_disable_stats (self);

  if (self->state == SP_WINDOW_STATE_FAILED)
    return;

  writer = sp_profiler_get_writer (profiler);
  reader = sp_capture_writer_create_reader (writer, &error);

  if (reader == NULL)
    {
      sp_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", error->message);
      sp_window_set_state (self, SP_WINDOW_STATE_EMPTY);
      return;
    }

  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  self->reader = g_steal_pointer (&reader);

  sp_window_build_profile (self);
}

static void
sp_window_profiler_failed (SpWindow     *self,
                           const GError *reason,
                           SpProfiler   *profiler)
{
  g_assert (SP_IS_WINDOW (self));
  g_assert (reason != NULL);
  g_assert (SP_IS_PROFILER (profiler));

  sp_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", reason->message);
  sp_window_set_state (self, SP_WINDOW_STATE_FAILED);
}

static void
sp_window_set_profiler (SpWindow   *self,
                        SpProfiler *profiler)
{
  g_assert (SP_IS_WINDOW (self));
  g_assert (SP_IS_PROFILER (profiler));

  if (self->profiler != profiler)
    {
      if (self->profiler != NULL)
        {
          if (sp_profiler_get_is_running (self->profiler))
            sp_profiler_stop (self->profiler);
          sp_profiler_menu_button_set_profiler (self->profiler_menu_button, NULL);
          sp_recording_state_view_set_profiler (self->recording_view, NULL);
          g_clear_object (&self->profiler);
        }

      if (profiler != NULL)
        {
          if (!sp_profiler_get_is_mutable (profiler))
            {
              g_warning ("Ignoring attempt to set profiler to an already running session!");
              return;
            }

          self->profiler = g_object_ref (profiler);

          g_signal_connect_object (profiler,
                                   "stopped",
                                   G_CALLBACK (sp_window_profiler_stopped),
                                   self,
                                   G_CONNECT_SWAPPED);

          g_signal_connect_object (profiler,
                                   "failed",
                                   G_CALLBACK (sp_window_profiler_failed),
                                   self,
                                   G_CONNECT_SWAPPED);

          sp_profiler_menu_button_set_profiler (self->profiler_menu_button, profiler);
          sp_recording_state_view_set_profiler (self->recording_view, profiler);
        }
    }
}

static void
sp_window_open_capture (GSimpleAction *action,
                        GVariant      *variant,
                        gpointer       user_data)
{
  SpWindow *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SP_IS_WINDOW (self));

  sp_window_open_from_dialog (self);
}

static void
sp_window_save_capture (GSimpleAction *action,
                        GVariant      *variant,
                        gpointer       user_data)
{
  g_autoptr(SpCaptureReader) reader = NULL;
  SpWindow *self = user_data;
  GtkWidget *dialog;
  GtkResponseType response;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SP_IS_WINDOW (self));

  if (self->reader == NULL)
    return;

  reader = sp_capture_reader_ref (self->reader);

  dialog = gtk_file_chooser_dialog_new (_("Save Capture As"),
                                        GTK_WINDOW (self),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Cancel"), GTK_RESPONSE_CANCEL,
                                        _("Save"), GTK_RESPONSE_OK,
                                        NULL);

  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      g_autofree gchar *filename = NULL;
      g_autoptr(GError) error = NULL;

      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

      if (filename == NULL)
        goto failure;

      if (!g_str_has_suffix (filename, ".syscap"))
        {
          gchar *tmp;

          tmp = g_strdup_printf ("%s.syscap", filename);
          g_free (filename);
          filename = tmp;
        }

      /* this should really be done outside the main loop. */
      if (!sp_capture_reader_save_as (reader, filename, &error))
        {
          sp_window_notify_user (self,
                                 GTK_MESSAGE_ERROR,
                                 _("An error occurred while attempting to save your capture: %s"),
                                 error->message);
          goto failure;
        }
    }

failure:
  gtk_widget_destroy (dialog);
}

static void
sp_window_close_capture (GSimpleAction *action,
                         GVariant      *variant,
                         gpointer       user_data)
{
  SpWindow *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SP_IS_WINDOW (self));

  sp_window_set_state (self, SP_WINDOW_STATE_EMPTY);
}

static void
sp_window_record_button_clicked (SpWindow  *self,
                                 GtkButton *button)
{
  g_assert (SP_IS_WINDOW (self));
  g_assert (GTK_IS_BUTTON (button));

  sp_window_start_recording (self);
}

static void
sp_window_screenshot (GSimpleAction *action,
                      GVariant      *variant,
                      gpointer       user_data)
{
  SpWindow *self = user_data;
  g_autofree gchar *str = NULL;
  GtkWindow *window;
  GtkScrolledWindow *scroller;
  GtkTextView *text_view;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (SP_IS_WINDOW (self));

  if (NULL == (str = sp_callgraph_view_screenshot (self->callgraph_view)))
    return;

  window = g_object_new (GTK_TYPE_WINDOW,
                         "title", "Sysprof",
                         "default-width", 800,
                         "default-height", 600,
                         "transient-for", self,
                         NULL);

  scroller = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (scroller));

  text_view = g_object_new (GTK_TYPE_TEXT_VIEW,
                            "editable", FALSE,
                            "monospace", TRUE,
                            "visible", TRUE,
                            NULL);
  gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (text_view));

  gtk_text_buffer_set_text (gtk_text_view_get_buffer (text_view), str, -1);

  gtk_window_present (window);
}

static void
sp_window_destroy (GtkWidget *widget)
{
  SpWindow *self = (SpWindow *)widget;

  g_clear_object (&self->profiler);
  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  sp_window_disable_stats (self);

  GTK_WIDGET_CLASS (sp_window_parent_class)->destroy (widget);
}

static void
sp_window_constructed (GObject *object)
{
  SpWindow *self = (SpWindow *)object;
  g_autoptr(SpProfiler) profiler = NULL;

  G_OBJECT_CLASS (sp_window_parent_class)->constructed (object);

  profiler = sp_local_profiler_new ();
  sp_window_set_profiler (self, profiler);

  sp_window_set_state (self, SP_WINDOW_STATE_EMPTY);
}

static void
sp_window_class_init (SpWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkBindingSet *binding_set;

  object_class->constructed = sp_window_constructed;

  widget_class->destroy = sp_window_destroy;

  signals [START_RECORDING] =
    g_signal_new_class_handler ("start-recording",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (sp_window_start_recording),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [STOP_RECORDING] =
    g_signal_new_class_handler ("stop-recording",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (sp_window_stop_recording),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  binding_set = gtk_binding_set_by_class (klass);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "stop-recording", 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sp-window.ui");
  gtk_widget_class_bind_template_child (widget_class, SpWindow, callgraph_view);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, empty_view);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, gear_menu_button);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, info_bar);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, info_bar_label);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, info_bar_revealer);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, profiler_menu_button);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, record_button);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, recording_view);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, stat_label);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, title);
  gtk_widget_class_bind_template_child (widget_class, SpWindow, view_stack);
}

static void
sp_window_init (SpWindow *self)
{
  GAction *action;
  static GActionEntry action_entries[] = {
    { "close-capture", sp_window_close_capture },
    { "open-capture",  sp_window_open_capture },
    { "save-capture",  sp_window_save_capture },
    { "screenshot",  sp_window_screenshot },
  };
  GtkApplication *app;
  GMenu *menu;

  gtk_widget_init_template (GTK_WIDGET (self));

  /*
   * Hookup widget signals.
   */

  g_signal_connect_object (self->info_bar,
                           "response",
                           G_CALLBACK (sp_window_hide_info_bar_revealer),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->info_bar,
                           "close",
                           G_CALLBACK (sp_window_hide_info_bar_revealer),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->record_button,
                           "clicked",
                           G_CALLBACK (sp_window_record_button_clicked),
                           self,
                           G_CONNECT_SWAPPED);

  /*
   * Setup actions for the window.
   */

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   self);

  /*
   * Setup our gear (hamburger) menu.
   */

  app = GTK_APPLICATION (g_application_get_default ());
  menu = gtk_application_get_menu_by_id (app, "gear-menu");
  gtk_menu_button_set_menu_model (self->gear_menu_button, G_MENU_MODEL (menu));

  /*
   * Restore previous window settings.
   */

  sp_window_settings_register (GTK_WINDOW (self));

  /*
   * Set default focus to the record button for quick workflow of
   * launch, enter, escape, view.
   */
  gtk_window_set_focus (GTK_WINDOW (self), GTK_WIDGET (self->record_button));
}

static void
sp_window_open_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  SpWindow *self = (SpWindow *)object;
  g_autoptr(SpCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SP_IS_WINDOW (self));
  g_assert (G_IS_TASK (result));

  reader = g_task_propagate_pointer (G_TASK (result), &error);

  if (reader == NULL)
    {
      sp_window_notify_user (self,
                             GTK_MESSAGE_ERROR,
                             "%s", error->message);
      return;
    }

  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  self->reader = g_steal_pointer (&reader);

  sp_window_set_state (self, SP_WINDOW_STATE_PROCESSING);
}

static void
sp_window_open_worker (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  g_autofree gchar *path = NULL;
  SpCaptureReader *reader;
  GFile *file = task_data;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SP_IS_WINDOW (source_object));
  g_assert (G_IS_FILE (file));

  path = g_file_get_path (file);

  if (NULL == (reader = sp_capture_reader_new (path, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_pointer (task, reader, (GDestroyNotify)sp_capture_reader_unref);
}

void
sp_window_open (SpWindow *self,
                GFile    *file)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SP_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  if (!g_file_is_native (file))
    {
      sp_window_notify_user (self,
                             GTK_MESSAGE_ERROR,
                             _("The file \"%s\" could not be opened. Only local files are supported."),
                             g_file_get_uri (file));
      return;
    }

  task = g_task_new (self, NULL, sp_window_open_cb, NULL);
  g_task_set_task_data (task, g_object_ref (file), g_object_unref);
  g_task_run_in_thread (task, sp_window_open_worker);
}

SpWindowState
sp_window_get_state (SpWindow *self)
{
  g_return_val_if_fail (SP_IS_WINDOW (self), SP_WINDOW_STATE_0);

  return self->state;
}

void
sp_window_open_from_dialog (SpWindow *self)
{
  GtkFileFilter *filter;
  GtkDialog *dialog;

  g_assert (SP_IS_WINDOW (self));

  dialog = g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
                         "action", GTK_FILE_CHOOSER_ACTION_OPEN,
                         "title", _("Open Capture"),
                         "transient-for", self,
                         NULL);

  gtk_dialog_add_buttons (dialog,
                          _("Cancel"), GTK_RESPONSE_CANCEL,
                          _("Open"), GTK_RESPONSE_OK,
                          NULL);

  gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Sysprof Captures"));
  gtk_file_filter_add_pattern (filter, "*.syscap");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  if (gtk_dialog_run (dialog) == GTK_RESPONSE_OK)
    {
      g_autoptr(GFile) file = NULL;

      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
      sp_window_open (self, file);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
}
