/* sysprof-window.c
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-window"

#include "config.h"

#include <errno.h>
#include <glib/gi18n.h>
#include <math.h>
#include <sysprof-ui.h>

#include "sysprof-application.h"
#include "sysprof-window.h"
#include "sysprof-window-settings.h"

struct _SysprofWindow
{
  GtkApplicationWindow  parent_instance;

  SysprofWindowState         state;

  SysprofProfiler           *profiler;
  SysprofCaptureReader      *reader;

  GCancellable         *refilter_cancellable;

  /* Gtk widget template children */
  SysprofCallgraphView      *callgraph_view;
  SysprofEmptyStateView     *empty_view;
  GtkMenuButton        *gear_menu_button;
  GtkInfoBar           *info_bar;
  GtkLabel             *info_bar_label;
  GtkRevealer          *info_bar_revealer;
  GtkPaned             *paned;
  SysprofProfilerMenuButton *profiler_menu_button;
  SysprofRecordingStateView *recording_view;
  GtkButton            *record_button;
  GtkLabel             *subtitle;
  GtkLabel             *stat_label;
  GtkLabel             *title;
  GtkStack             *view_stack;
  SysprofVisualizerView     *visualizers;
  SysprofZoomManager        *zoom_manager;
  GtkLabel             *zoom_one_label;

  guint                 stats_handler;

  guint                 closing : 1;
};

G_DEFINE_TYPE (SysprofWindow, sysprof_window, GTK_TYPE_APPLICATION_WINDOW)

static void sysprof_window_set_state (SysprofWindow      *self,
                                 SysprofWindowState  state);

enum {
  START_RECORDING,
  STOP_RECORDING,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void sysprof_window_set_profiler (SysprofWindow   *self,
                                    SysprofProfiler *profiler);

static G_GNUC_PRINTF(3, 4) void
sysprof_window_notify_user (SysprofWindow       *self,
                       GtkMessageType  message_type,
                       const gchar    *format,
                       ...)
{
  g_autofree gchar *str = NULL;
  va_list args;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  gtk_info_bar_set_message_type (self->info_bar, message_type);
  gtk_label_set_label (self->info_bar_label, str);
  gtk_revealer_set_reveal_child (self->info_bar_revealer, TRUE);
}

static void
sysprof_window_action_set (SysprofWindow    *self,
                      const gchar *action_name,
                      const gchar *first_property,
                      ...)
{
  gpointer action;
  va_list args;

  g_assert (SYSPROF_IS_WINDOW (self));
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
sysprof_window_update_stats (gpointer data)
{
  SysprofWindow *self = data;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->profiler != NULL)
    {
      SysprofCaptureWriter *writer;

      if (NULL != (writer = sysprof_profiler_get_writer (self->profiler)))
        {
          g_autofree gchar *str = NULL;
          SysprofCaptureStat stbuf;
          guint count;

          sysprof_capture_writer_stat (writer, &stbuf);

          count = stbuf.frame_count[SYSPROF_CAPTURE_FRAME_SAMPLE];
          /* Translators: %u is the number (amount) of samples. */
          str = g_strdup_printf (_("Samples: %u"), count);
          gtk_label_set_label (self->stat_label, str);
        }
    }

  return G_SOURCE_CONTINUE;
}


static void
sysprof_window_update_subtitle (SysprofWindow *self)
{
  g_autofree gchar *relative = NULL;
  const gchar *filename;
  const gchar *date;
  GTimeVal tv;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (self->reader != NULL);

  if (NULL != (filename = sysprof_capture_reader_get_filename (self->reader)))
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

  date = sysprof_capture_reader_get_time (self->reader);

  if (g_time_val_from_iso8601 (date, &tv))
    {
      g_autoptr(GDateTime) dt = NULL;
      g_autofree gchar *str = NULL;
      g_autofree gchar *label = NULL;

      dt = g_date_time_new_from_timeval_local (&tv);
      str = g_date_time_format (dt, "%x %X");

      /* Translators: The first %s is a file name, the second is the date and time. */
      label = g_strdup_printf (_("%s — %s"), filename, str);

      gtk_label_set_label (self->subtitle, label);
    }
  else
    gtk_label_set_label (self->subtitle, filename);
}

static void
sysprof_window_build_profile_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  SysprofProfile *profile = (SysprofProfile *)object;
  g_autoptr(SysprofWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (profile));
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);

  if (!sysprof_profile_generate_finish (profile, result, &error))
    {
      /* If we were cancelled while updating the selection, ignore the failure */
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
          (self->state == SYSPROF_WINDOW_STATE_BROWSING))
        return;
      sysprof_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", error->message);
      sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_EMPTY);
      return;
    }

  sysprof_callgraph_view_set_profile (self->callgraph_view, SYSPROF_CALLGRAPH_PROFILE (profile));
  if (sysprof_callgraph_view_get_n_functions (self->callgraph_view) == 0)
    sysprof_window_notify_user (self,
                           GTK_MESSAGE_WARNING,
                           _("Not enough samples were collected to generate a callgraph"));

  sysprof_visualizer_view_set_reader (self->visualizers, self->reader);

  sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_BROWSING);
}

static void
sysprof_window_build_profile (SysprofWindow *self)
{
  g_autoptr(SysprofProfile) profile = NULL;
  SysprofSelection *selection;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (self->reader != NULL);

  if (self->refilter_cancellable != NULL)
    {
      if (!g_cancellable_is_cancelled (self->refilter_cancellable))
        g_cancellable_cancel (self->refilter_cancellable);
      g_clear_object (&self->refilter_cancellable);
    }

  selection = sysprof_visualizer_view_get_selection (self->visualizers);

  profile = sysprof_callgraph_profile_new_with_selection (selection);
  sysprof_profile_set_reader (profile, self->reader);

  self->refilter_cancellable = g_cancellable_new ();

  gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), FALSE);

  sysprof_profile_generate (profile,
                       self->refilter_cancellable,
                       sysprof_window_build_profile_cb,
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
sysprof_window_set_state (SysprofWindow      *self,
                     SysprofWindowState  state)
{
  g_autoptr(SysprofProfiler) profiler = NULL;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->state == state)
    return;

  self->state = state;

  switch (state)
    {
    case SYSPROF_WINDOW_STATE_EMPTY:
    case SYSPROF_WINDOW_STATE_FAILED:
      /* Translators: This is a button. */
      gtk_button_set_label (self->record_button, _("Record"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      add_class (self->record_button, "suggested-action");
      remove_class (self->record_button, "destructive-action");
      if (state == SYSPROF_WINDOW_STATE_FAILED)
        gtk_stack_set_visible_child_name (self->view_stack, "failed");
      else
        gtk_stack_set_visible_child_name (self->view_stack, "empty");
      gtk_label_set_label (self->subtitle, _("Not running"));
      sysprof_callgraph_view_set_profile (self->callgraph_view, NULL);
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), FALSE);
      g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
      sysprof_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      profiler = sysprof_local_profiler_new ();
      sysprof_window_set_profiler (self, profiler);
      break;

    case SYSPROF_WINDOW_STATE_RECORDING:
      /* Translators: This is a button. */
      gtk_button_set_label (self->record_button, _("Stop"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      remove_class (self->record_button, "suggested-action");
      add_class (self->record_button, "destructive-action");
      gtk_stack_set_visible_child_name (self->view_stack, "recording");
      gtk_label_set_label (self->subtitle, _("Recording…"));
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), TRUE);
      g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
      sysprof_callgraph_view_set_profile (self->callgraph_view, NULL);
      sysprof_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      break;

    case SYSPROF_WINDOW_STATE_PROCESSING:
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), FALSE);
      gtk_label_set_label (self->subtitle, _("Building profile…"));
      sysprof_window_action_set (self, "close-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "save-capture", "enabled", FALSE, NULL);
      sysprof_window_action_set (self, "screenshot", "enabled", FALSE, NULL);
      sysprof_window_build_profile (self);
      break;

    case SYSPROF_WINDOW_STATE_BROWSING:
      /* Translators: This is a button. */
      gtk_button_set_label (self->record_button, _("Record"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), TRUE);
      add_class (self->record_button, "suggested-action");
      remove_class (self->record_button, "destructive-action");
      gtk_widget_set_visible (GTK_WIDGET (self->stat_label), TRUE);
      gtk_stack_set_visible_child_name (self->view_stack, "browsing");
      sysprof_window_update_stats (self);
      sysprof_window_update_subtitle (self);
      sysprof_window_action_set (self, "close-capture", "enabled", TRUE, NULL);
      sysprof_window_action_set (self, "save-capture", "enabled", TRUE, NULL);
      sysprof_window_action_set (self, "screenshot", "enabled", TRUE, NULL);
      profiler = sysprof_local_profiler_new ();
      sysprof_window_set_profiler (self, profiler);
      break;

    case SYSPROF_WINDOW_STATE_0:
    default:
      g_warning ("Unknown state: %0d", state);
      break;
    }
}

static void
sysprof_window_enable_stats (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->stats_handler == 0)
    self->stats_handler =
      g_timeout_add_seconds (1, sysprof_window_update_stats, self);
}

static void
sysprof_window_disable_stats (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->stats_handler != 0)
    {
      g_source_remove (self->stats_handler);
      self->stats_handler = 0;
    }
}

static void
sysprof_window_add_sources (SysprofWindow   *window,
                       SysprofProfiler *profiler)
{
#ifdef __linux__
  g_autoptr(SysprofSource) host_source = NULL;
  g_autoptr(SysprofSource) proc_source = NULL;
  g_autoptr(SysprofSource) perf_source = NULL;
  g_autoptr(SysprofSource) memory_source = NULL;

  g_assert (SYSPROF_IS_WINDOW (window));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  proc_source = sysprof_proc_source_new ();
  sysprof_profiler_add_source (profiler, proc_source);

  perf_source = sysprof_perf_source_new ();
  sysprof_profiler_add_source (profiler, perf_source);

  host_source = sysprof_hostinfo_source_new ();
  sysprof_profiler_add_source (profiler, host_source);

  memory_source = sysprof_memory_source_new ();
  sysprof_profiler_add_source (profiler, memory_source);
#endif
}

static void
sysprof_window_start_recording (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  if ((self->state == SYSPROF_WINDOW_STATE_EMPTY) ||
      (self->state == SYSPROF_WINDOW_STATE_FAILED) ||
      (self->state == SYSPROF_WINDOW_STATE_BROWSING))
    {
      gtk_revealer_set_reveal_child (self->info_bar_revealer, FALSE);
      sysprof_window_add_sources (self, self->profiler);
      sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_RECORDING);
      sysprof_window_enable_stats (self);
      sysprof_profiler_start (self->profiler);
      return;
    }
}

static void
sysprof_window_stop_recording (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->state == SYSPROF_WINDOW_STATE_RECORDING)
    {
      if (self->profiler != NULL)
        {
          /* SysprofProfiler::stopped will move us to generating */
          gtk_label_set_label (self->subtitle, _("Stopping…"));
          /*
           * In case that ::stopped takes a while to execute,
           * disable record button immediately.
           */
          gtk_widget_set_sensitive (GTK_WIDGET (self->record_button), FALSE);
          sysprof_profiler_stop (self->profiler);
        }
    }
}

static void
sysprof_window_hide_info_bar_revealer (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  gtk_revealer_set_reveal_child (self->info_bar_revealer, FALSE);
}

static void
sysprof_window_profiler_stopped (SysprofWindow   *self,
                            SysprofProfiler *profiler)
{
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureWriter *writer;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  sysprof_window_disable_stats (self);

  if (self->closing)
    {
      gtk_window_close (GTK_WINDOW (self));
      return;
    }

  if (self->state == SYSPROF_WINDOW_STATE_FAILED)
    return;

  writer = sysprof_profiler_get_writer (profiler);
  reader = sysprof_capture_writer_create_reader (writer, &error);

  if (reader == NULL)
    {
      sysprof_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", error->message);
      sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_EMPTY);
      return;
    }

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  self->reader = g_steal_pointer (&reader);

  sysprof_window_build_profile (self);
}

static void
sysprof_window_profiler_failed (SysprofWindow     *self,
                           const GError *reason,
                           SysprofProfiler   *profiler)
{
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (reason != NULL);
  g_assert (SYSPROF_IS_PROFILER (profiler));

  sysprof_window_notify_user (self, GTK_MESSAGE_ERROR, "%s", reason->message);
  sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_FAILED);
}

static void
sysprof_window_set_profiler (SysprofWindow   *self,
                        SysprofProfiler *profiler)
{
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  if (self->profiler != profiler)
    {
      if (self->profiler != NULL)
        {
          if (sysprof_profiler_get_is_running (self->profiler))
            sysprof_profiler_stop (self->profiler);
          sysprof_profiler_menu_button_set_profiler (self->profiler_menu_button, NULL);
          sysprof_recording_state_view_set_profiler (self->recording_view, NULL);
          g_clear_object (&self->profiler);
        }

      if (profiler != NULL)
        {
          if (!sysprof_profiler_get_is_mutable (profiler))
            {
              g_warning ("Ignoring attempt to set profiler to an already running session!");
              return;
            }

          self->profiler = g_object_ref (profiler);

          g_signal_connect_object (profiler,
                                   "stopped",
                                   G_CALLBACK (sysprof_window_profiler_stopped),
                                   self,
                                   G_CONNECT_SWAPPED);

          g_signal_connect_object (profiler,
                                   "failed",
                                   G_CALLBACK (sysprof_window_profiler_failed),
                                   self,
                                   G_CONNECT_SWAPPED);

          sysprof_profiler_menu_button_set_profiler (self->profiler_menu_button, profiler);
          sysprof_recording_state_view_set_profiler (self->recording_view, profiler);
        }
    }
}

static void
sysprof_window_open_capture (GSimpleAction *action,
                        GVariant      *variant,
                        gpointer       user_data)
{
  SysprofWindow *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SYSPROF_IS_WINDOW (self));

  sysprof_window_open_from_dialog (self);
}

static void
sysprof_window_save_capture (GSimpleAction *action,
                        GVariant      *variant,
                        gpointer       user_data)
{
  g_autoptr(SysprofCaptureReader) reader = NULL;
  SysprofWindow *self = user_data;
  GtkFileChooserNative *dialog;
  gint response;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->reader == NULL)
    {
      g_warning ("Save called without a capture open, ignoring");
      return;
    }

  reader = sysprof_capture_reader_ref (self->reader);

  /* Translators: This is a window title. */
  dialog = gtk_file_chooser_native_new (_("Save Capture As…"),
                                        GTK_WINDOW (self),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        /* Translators: This is a button. */
                                        _("Save"),
                                        /* Translators: This is a button. */
                                        _("Cancel"));

  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), TRUE);

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
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
      if (!sysprof_capture_reader_save_as (reader, filename, &error))
        {
          sysprof_window_notify_user (self,
                                 GTK_MESSAGE_ERROR,
                                 /* Translators: %s is the error message. */
                                 _("An error occurred while attempting to save your capture: %s"),
                                 error->message);
          goto failure;
        }
    }

failure:
  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (dialog));
}

static void
sysprof_window_close_capture (GSimpleAction *action,
                         GVariant      *variant,
                         gpointer       user_data)
{
  SysprofWindow *self = user_data;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);
  g_assert (SYSPROF_IS_WINDOW (self));

  sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_EMPTY);
}

static void
sysprof_window_record_button_clicked (SysprofWindow  *self,
                                 GtkButton *button)
{
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (GTK_IS_BUTTON (button));

  if (self->state == SYSPROF_WINDOW_STATE_RECORDING)
    sysprof_window_stop_recording (self);
  else
    sysprof_window_start_recording (self);
}

static void
sysprof_window_screenshot (GSimpleAction *action,
                      GVariant      *variant,
                      gpointer       user_data)
{
  SysprofWindow *self = user_data;
  g_autofree gchar *str = NULL;
  GtkWindow *window;
  GtkScrolledWindow *scroller;
  GtkTextView *text_view;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (SYSPROF_IS_WINDOW (self));

  if (NULL == (str = sysprof_callgraph_view_screenshot (self->callgraph_view)))
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

static gboolean
sysprof_window_delete_event (GtkWidget   *widget,
                        GdkEventAny *event)
{
  SysprofWindow *self = (SysprofWindow *)widget;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (event != NULL);

  if (self->state == SYSPROF_WINDOW_STATE_RECORDING)
    {
      if (self->profiler != NULL)
        {
          if (self->closing == FALSE)
            {
              self->closing = TRUE;
              sysprof_profiler_stop (self->profiler);
              return GDK_EVENT_STOP;
            }
        }
    }

  return GDK_EVENT_PROPAGATE;
}

static gboolean
zoom_level_to_string (GBinding     *binding,
                      const GValue *from_value,
                      GValue       *to_value,
                      gpointer      user_data)
{
  gdouble percent = 100.0 * g_value_get_double (from_value);
  g_value_take_string (to_value, g_strdup_printf ("%u%%", (guint)floor (percent)));
  return TRUE;
}

static void
sysprof_window_visualizers_selection_changed (SysprofWindow              *self,
                                         SysprofSelection *selection)
{
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_SELECTION (selection));

  sysprof_window_build_profile (self);
}

static void
sysprof_window_destroy (GtkWidget *widget)
{
  SysprofWindow *self = (SysprofWindow *)widget;

  if (self->refilter_cancellable != NULL)
    {
      if (!g_cancellable_is_cancelled (self->refilter_cancellable))
        g_cancellable_cancel (self->refilter_cancellable);
      g_clear_object (&self->refilter_cancellable);
    }

  g_clear_object (&self->profiler);
  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  sysprof_window_disable_stats (self);

  GTK_WIDGET_CLASS (sysprof_window_parent_class)->destroy (widget);
}

static void
sysprof_window_constructed (GObject *object)
{
  SysprofWindow *self = (SysprofWindow *)object;
  g_autoptr(SysprofProfiler) profiler = NULL;

  G_OBJECT_CLASS (sysprof_window_parent_class)->constructed (object);

  profiler = sysprof_local_profiler_new ();
  sysprof_window_set_profiler (self, profiler);

  sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_EMPTY);
}

static void
sysprof_window_class_init (SysprofWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkBindingSet *binding_set;

  object_class->constructed = sysprof_window_constructed;

  widget_class->delete_event = sysprof_window_delete_event;
  widget_class->destroy = sysprof_window_destroy;

  signals [START_RECORDING] =
    g_signal_new_class_handler ("start-recording",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (sysprof_window_start_recording),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [STOP_RECORDING] =
    g_signal_new_class_handler ("stop-recording",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (sysprof_window_stop_recording),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  binding_set = gtk_binding_set_by_class (klass);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "stop-recording", 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-window.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, callgraph_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, empty_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, gear_menu_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, info_bar);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, info_bar_label);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, info_bar_revealer);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, paned);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, profiler_menu_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, record_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, recording_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, stat_label);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, title);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, view_stack);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, visualizers);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, zoom_manager);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, zoom_one_label);

  g_type_ensure (SYSPROF_TYPE_MARKS_VIEW);
}

static void
sysprof_window_init (SysprofWindow *self)
{
  static GActionEntry action_entries[] = {
    { "close-capture", sysprof_window_close_capture },
    { "open-capture",  sysprof_window_open_capture },
    { "save-capture",  sysprof_window_save_capture },
    { "screenshot",  sysprof_window_screenshot },
  };
  SysprofSelection *selection;
  g_autoptr(GtkWindowGroup) window_group = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

#ifdef DEVELOPMENT_BUILD
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "development-version");
#endif

  /*
   * Hookup widget signals.
   */

  g_signal_connect_object (self->info_bar,
                           "response",
                           G_CALLBACK (sysprof_window_hide_info_bar_revealer),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->info_bar,
                           "close",
                           G_CALLBACK (sysprof_window_hide_info_bar_revealer),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->record_button,
                           "clicked",
                           G_CALLBACK (sysprof_window_record_button_clicked),
                           self,
                           G_CONNECT_SWAPPED);

  g_object_bind_property_full (self->zoom_manager, "zoom", self->zoom_one_label, "label",
                               G_BINDING_SYNC_CREATE,
                               zoom_level_to_string, NULL, NULL, NULL);

  /*
   * Wire up selections for visualizers to update callgraph.
   */

  selection = sysprof_visualizer_view_get_selection (self->visualizers);

  g_signal_connect_object (selection,
                           "changed",
                           G_CALLBACK (sysprof_window_visualizers_selection_changed),
                           self,
                           G_CONNECT_SWAPPED);

  /*
   * Setup actions for the window.
   */
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "zoom", G_ACTION_GROUP (self->zoom_manager));

  /*
   * Restore previous window settings.
   */
  sysprof_window_settings_register (GTK_WINDOW (self));

  /*
   * Set default focus to the record button for quick workflow of
   * launch, enter, escape, view.
   */
  gtk_window_set_focus (GTK_WINDOW (self), GTK_WIDGET (self->record_button));

  /*
   * Prevent grabs (e.g. modal dialogs) from affecting multiple windows.
   */
  window_group = gtk_window_group_new ();
  gtk_window_group_add_window (window_group, GTK_WINDOW (self));
}

static void
sysprof_window_open_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  SysprofWindow *self = (SysprofWindow *)object;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (G_IS_TASK (result));

  reader = g_task_propagate_pointer (G_TASK (result), &error);

  if (reader == NULL)
    {
      sysprof_window_notify_user (self,
                             GTK_MESSAGE_ERROR,
                             "%s", error->message);
      return;
    }

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  self->reader = g_steal_pointer (&reader);

  sysprof_window_set_state (self, SYSPROF_WINDOW_STATE_PROCESSING);
}

static void
sysprof_window_open_worker (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  g_autofree gchar *path = NULL;
  SysprofCaptureReader *reader;
  GFile *file = task_data;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_WINDOW (source_object));
  g_assert (G_IS_FILE (file));

  path = g_file_get_path (file);

  if (NULL == (reader = sysprof_capture_reader_new (path, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_pointer (task, reader, (GDestroyNotify)sysprof_capture_reader_unref);
}

void
sysprof_window_open (SysprofWindow *self,
                GFile    *file)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  if (!g_file_is_native (file))
    {
      sysprof_window_notify_user (self,
                             GTK_MESSAGE_ERROR,
                             _("The file “%s” could not be opened. Only local files are supported."),
                             g_file_get_uri (file));
      return;
    }

  task = g_task_new (self, NULL, sysprof_window_open_cb, NULL);
  g_task_set_task_data (task, g_object_ref (file), g_object_unref);
  g_task_run_in_thread (task, sysprof_window_open_worker);
}

SysprofWindowState
sysprof_window_get_state (SysprofWindow *self)
{
  g_return_val_if_fail (SYSPROF_IS_WINDOW (self), SYSPROF_WINDOW_STATE_0);

  return self->state;
}

void
sysprof_window_open_from_dialog (SysprofWindow *self)
{
  GtkFileChooserNative *dialog;
  GtkFileFilter *filter;
  gint response;

  g_assert (SYSPROF_IS_WINDOW (self));

  /* Translators: This is a window title. */
  dialog = gtk_file_chooser_native_new (_("Open Capture…"),
                                        GTK_WINDOW (self),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        /* Translators: This is a button. */
                                        _("Open"),
                                        /* Translators: This is a button. */
                                        _("Cancel"));

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Sysprof Captures"));
  gtk_file_filter_add_pattern (filter, "*.syscap");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      g_autoptr(GFile) file = NULL;

      file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
      sysprof_window_open (self, file);
    }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (dialog));
}
