/* sysprof-window.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "sysprof-counters-section.h"
#include "sysprof-cpu-section.h"
#include "sysprof-dbus-section.h"
#include "sysprof-energy-section.h"
#include "sysprof-files-section.h"
#include "sysprof-graphics-section.h"
#include "sysprof-greeter.h"
#include "sysprof-logs-section.h"
#include "sysprof-marks-section.h"
#include "sysprof-memory-section.h"
#include "sysprof-metadata-section.h"
#include "sysprof-network-section.h"
#include "sysprof-pair.h"
#include "sysprof-processes-section.h"
#include "sysprof-samples-section.h"
#include "sysprof-session-filters-widget.h"
#include "sysprof-sidebar.h"
#include "sysprof-storage-section.h"
#include "sysprof-task-row.h"
#include "sysprof-util.h"
#include "sysprof-window.h"

struct _SysprofWindow
{
  AdwApplicationWindow   parent_instance;

  SysprofDocument       *document;
  SysprofDocumentLoader *loader;
  SysprofSession        *session;

  GtkToggleButton       *show_right_sidebar;
  GtkWidget             *left_split_overlay;
  GtkWidget             *right_split_overlay;
  GtkProgressBar        *progress_bar;
  AdwWindowTitle        *stack_title;

  guint                  disposed : 1;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_IS_LOADED,
  PROP_LOADER,
  PROP_SESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofWindow, sysprof_window, ADW_TYPE_APPLICATION_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
sysprof_window_update_zoom_actions (SysprofWindow *self)
{
  const SysprofTimeSpan *visible_time;
  const SysprofTimeSpan *document_time;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  visible_time = sysprof_session_get_visible_time (self->session);
  document_time = sysprof_session_get_document_time (self->session);

  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "session.zoom-one",
                                 !sysprof_time_span_equal (visible_time, document_time));
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "session.zoom-out",
                                 !sysprof_time_span_equal (visible_time, document_time));
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "session.seek-backward",
                                 visible_time->begin_nsec > document_time->begin_nsec);
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "session.seek-forward",
                                 visible_time->end_nsec < document_time->end_nsec);
}

static void
sysprof_window_update_action_state (SysprofWindow *self)
{
  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    {
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.zoom-one", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.zoom-out", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.zoom-in", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.seek-forward", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.seek-backward", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.save-capture", FALSE);
    }
  else
    {
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.save-capture", TRUE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "session.zoom-in", TRUE);

      sysprof_window_update_zoom_actions (self);
    }
}

static void
sysprof_window_open_file_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  GtkFileDialog *dialog = (GtkFileDialog *)object;
  g_autoptr(SysprofRecordingTemplate) template = NULL;
  g_autoptr(GtkWindow) transient_for = NULL;
  g_autoptr(SysprofPair) pair = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (!transient_for || GTK_IS_WINDOW (transient_for));

  g_object_get (pair,
                "first", &transient_for,
                "second", &template,
                NULL);

  g_assert (!transient_for || GTK_IS_WIDGET (transient_for));
  g_assert (!template || SYSPROF_IS_RECORDING_TEMPLATE (template));

  if ((file = gtk_file_dialog_open_finish (dialog, result, &error)))
    {
      if (g_file_is_native (file))
        {
          sysprof_window_open (SYSPROF_APPLICATION_DEFAULT, template, file);

          if (transient_for && !SYSPROF_IS_WINDOW (transient_for))
            gtk_window_destroy (transient_for);
        }
      else
        {
          GtkWidget *message;

          G_GNUC_BEGIN_IGNORE_DEPRECATIONS

          message = adw_message_dialog_new (NULL,
                                            _("Must Capture to Local File"),
                                            _("You must choose a local file to capture using Sysprof"));
          adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (message), "close", _("Close"));
          gtk_window_present (GTK_WINDOW (message));

          G_GNUC_END_IGNORE_DEPRECATIONS
        }
    }
}

void
sysprof_window_open_file (GtkWindow                *parent,
                          SysprofRecordingTemplate *template)
{
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GtkFileFilter) filter = NULL;
  g_autoptr(GListStore) filters = NULL;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Open Recording"));
  gtk_file_dialog_set_accept_label (dialog, _("Open"));
  gtk_file_dialog_set_modal (dialog, TRUE);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Sysprof Capture (*.syscap)"));
  gtk_file_filter_add_mime_type (filter, "application/x-sysprof-capture");
  gtk_file_filter_add_suffix (filter, "syscap");

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

  gtk_file_dialog_open (dialog,
                        parent,
                        NULL,
                        sysprof_window_open_file_cb,
                        g_object_new (SYSPROF_TYPE_PAIR,
                                      "first", parent,
                                      "second", template,
                                      NULL));
}

static void
sysprof_window_open_capture_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  g_autoptr(SysprofRecordingTemplate) template = NULL;

  /* Open for default state file so we can apply settings */
  template = sysprof_recording_template_new_from_file (NULL, NULL);

  sysprof_window_open_file (GTK_WINDOW (widget), template);
}

static void
sysprof_window_set_document (SysprofWindow   *self,
                             SysprofDocument *document)
{
  static const char *callgraph_actions[] = {
    "bottom-up",
    "categorize-frames",
    "hide-system-libraries",
    "ignore-kernel-processes",
    "ignore-process-0",
    "include-threads",
    "left-heavy",
    "merge-similar-processes",
  };
  g_autofree char *title = NULL;
  g_autofree char *full_title = NULL;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (!document || SYSPROF_IS_DOCUMENT (document));
  g_assert (self->document == NULL);
  g_assert (self->session == NULL);

  if (document == NULL)
    return;

  title = sysprof_document_dup_title (document);
  full_title = g_strdup_printf ("%s — %s", _("Sysprof"), title);
  gtk_window_set_title (GTK_WINDOW (self), full_title);

  g_set_object (&self->document, document);
  self->session = sysprof_session_new (document);

  g_signal_connect_object (self->session,
                           "notify::selected-time",
                           G_CALLBACK (sysprof_window_update_zoom_actions),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->session,
                           "notify::visible-time",
                           G_CALLBACK (sysprof_window_update_zoom_actions),
                           self,
                           G_CONNECT_SWAPPED);

  for (guint i = 0; i < G_N_ELEMENTS (callgraph_actions); i++)
    {
      g_autofree char *action_name = g_strdup_printf ("callgraph.%s", callgraph_actions[i]);
      g_autoptr(GPropertyAction) action = g_property_action_new (action_name, self->session, callgraph_actions[i]);

      g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (action));
    }

  sysprof_window_update_action_state (self);

  g_object_bind_property (self->document, "subtitle",
                          self->stack_title, "subtitle",
                          G_BINDING_SYNC_CREATE);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_LOADED]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

static void
main_view_notify_sidebar (SysprofWindow       *self,
                          GParamSpec          *pspec,
                          AdwOverlaySplitView *main_view)
{
  GtkWidget *sidebar;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (ADW_IS_OVERLAY_SPLIT_VIEW (main_view));

  if (self->disposed)
      return;

  sidebar = adw_overlay_split_view_get_sidebar (main_view);

  if (sidebar == NULL)
    adw_overlay_split_view_set_show_sidebar (main_view, FALSE);

  gtk_widget_set_sensitive (GTK_WIDGET (self->show_right_sidebar), sidebar != NULL);
}

static gboolean
n_filters_to_button_visibility (SysprofWindow *self,
                                unsigned int   n_filters)
{
  return n_filters > 0;
}

static void
sysprof_window_session_seek_backward (GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  const SysprofTimeSpan *document_time;
  const SysprofTimeSpan *visible_time;
  SysprofTimeSpan select;
  gint64 duration;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  visible_time = sysprof_session_get_visible_time (self->session);
  document_time = sysprof_session_get_document_time (self->session);
  duration = sysprof_time_span_duration (*visible_time);

  select.begin_nsec = MAX (document_time->begin_nsec, visible_time->begin_nsec - duration);
  select.end_nsec = select.begin_nsec + duration;

  sysprof_session_select_time (self->session, &select);
  sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_window_session_seek_forward (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  const SysprofTimeSpan *document_time;
  const SysprofTimeSpan *visible_time;
  SysprofTimeSpan select;
  gint64 duration;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  visible_time = sysprof_session_get_visible_time (self->session);
  document_time = sysprof_session_get_document_time (self->session);
  duration = sysprof_time_span_duration (*visible_time);

  select.begin_nsec = MIN (document_time->end_nsec - duration, visible_time->begin_nsec + duration);
  select.end_nsec = select.begin_nsec + duration;

  sysprof_session_select_time (self->session, &select);
  sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_window_session_zoom_one (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  sysprof_session_select_time (self->session,
                               sysprof_session_get_document_time (self->session));
}

static void
sysprof_window_session_zoom_in (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  const SysprofTimeSpan *visible_time;
  SysprofTimeSpan select;
  gint64 duration;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  visible_time = sysprof_session_get_visible_time (self->session);
  duration = sysprof_time_span_duration (*visible_time);

  select.begin_nsec = visible_time->begin_nsec;
  select.end_nsec = select.begin_nsec + (duration / 2);

  sysprof_session_select_time (self->session, &select);
  sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_window_session_zoom_out (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  SysprofTimeSpan select;
  gint64 duration;

  g_assert (SYSPROF_IS_WINDOW (self));

  if (self->session == NULL)
    return;

  select = *sysprof_session_get_visible_time (self->session);
  duration = sysprof_time_span_duration (select);
  select.end_nsec += duration;

  sysprof_session_select_time (self->session, &select);
  sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_window_write_document_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(SysprofWindow) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_WINDOW (self));

  if (!sysprof_document_save_finish (document, result, &error))
    g_warning ("%s", error->message);
}

static void
sysprof_window_save_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  GtkFileDialog *dialog = (GtkFileDialog *)object;
  g_autoptr(SysprofWindow) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_WINDOW (self));

  if ((file = gtk_file_dialog_save_finish (dialog, result, &error)))
    sysprof_document_save_async (self->document,
                                 file,
                                 NULL,
                                 sysprof_window_write_document_cb,
                                 g_object_ref (self));
}

static void
sysprof_window_save_capture (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  g_autoptr(GtkFileDialog) dialog = NULL;

  g_assert (SYSPROF_IS_WINDOW (self));

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Save to File"));
  gtk_file_dialog_set_accept_label (dialog, _("Save"));
  gtk_file_dialog_set_modal (dialog, TRUE);
  gtk_file_dialog_set_initial_name (dialog, "capture.syscap");

  gtk_file_dialog_save (dialog,
                        GTK_WINDOW (self),
                        NULL,
                        sysprof_window_save_cb,
                        g_object_ref (self));
}

static void
sysprof_window_dispose (GObject *object)
{
  SysprofWindow *self = (SysprofWindow *)object;

  self->disposed = TRUE;

  if (self->right_split_overlay)
    adw_overlay_split_view_set_sidebar (ADW_OVERLAY_SPLIT_VIEW (self->right_split_overlay), NULL);

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_WINDOW);

  g_clear_object (&self->document);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (sysprof_window_parent_class)->dispose (object);
}

static void
sysprof_window_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  SysprofWindow *self = SYSPROF_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_window_get_document (self));
      break;

    case PROP_LOADER:
      g_value_set_object (value, self->loader);
      break;

    case PROP_IS_LOADED:
      g_value_set_boolean (value, !!sysprof_window_get_document (self));
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_window_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_window_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  SysprofWindow *self = SYSPROF_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      sysprof_window_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_window_class_init (SysprofWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_window_dispose;
  object_class->get_property = sysprof_window_get_property;
  object_class->set_property = sysprof_window_set_property;

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_LOADER] =
    g_param_spec_object ("loader", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_LOADER,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IS_LOADED] =
    g_param_spec_boolean ("is-loaded", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-window.ui");

  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, left_split_overlay);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, right_split_overlay);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, show_right_sidebar);
  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, stack_title);

  gtk_widget_class_bind_template_callback (widget_class, main_view_notify_sidebar);
  gtk_widget_class_bind_template_callback (widget_class, n_filters_to_button_visibility);

  gtk_widget_class_install_action (widget_class, "win.open-capture", NULL, sysprof_window_open_capture_action);
  gtk_widget_class_install_action (widget_class, "session.zoom-one", NULL, sysprof_window_session_zoom_one);
  gtk_widget_class_install_action (widget_class, "session.zoom-out", NULL, sysprof_window_session_zoom_out);
  gtk_widget_class_install_action (widget_class, "session.zoom-in", NULL, sysprof_window_session_zoom_in);
  gtk_widget_class_install_action (widget_class, "session.seek-forward", NULL, sysprof_window_session_seek_forward);
  gtk_widget_class_install_action (widget_class, "session.seek-backward", NULL, sysprof_window_session_seek_backward);
  gtk_widget_class_install_action (widget_class, "win.save-capture", NULL, sysprof_window_save_capture);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_plus, GDK_CONTROL_MASK, "session.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_equal, GDK_CONTROL_MASK, "session.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_minus, GDK_CONTROL_MASK, "session.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_0, GDK_CONTROL_MASK, "session.zoom-one", NULL);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_bracketleft, GDK_CONTROL_MASK, "session.seek-backward", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_bracketright, GDK_CONTROL_MASK, "session.seek-forward", NULL);

  g_type_ensure (SYSPROF_TYPE_COUNTERS_SECTION);
  g_type_ensure (SYSPROF_TYPE_CPU_SECTION);
  g_type_ensure (SYSPROF_TYPE_DBUS_SECTION);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT);
  g_type_ensure (SYSPROF_TYPE_ENERGY_SECTION);
  g_type_ensure (SYSPROF_TYPE_FILES_SECTION);
  g_type_ensure (SYSPROF_TYPE_GRAPHICS_SECTION);
  g_type_ensure (SYSPROF_TYPE_LOGS_SECTION);
  g_type_ensure (SYSPROF_TYPE_MARKS_SECTION);
  g_type_ensure (SYSPROF_TYPE_MEMORY_SECTION);
  g_type_ensure (SYSPROF_TYPE_METADATA_SECTION);
  g_type_ensure (SYSPROF_TYPE_NETWORK_SECTION);
  g_type_ensure (SYSPROF_TYPE_PROCESSES_SECTION);
  g_type_ensure (SYSPROF_TYPE_SAMPLES_SECTION);
  g_type_ensure (SYSPROF_TYPE_SESSION_FILTERS_WIDGET);
  g_type_ensure (SYSPROF_TYPE_STORAGE_SECTION);
  g_type_ensure (SYSPROF_TYPE_SESSION);
  g_type_ensure (SYSPROF_TYPE_SYMBOL);
  g_type_ensure (SYSPROF_TYPE_SIDEBAR);
  g_type_ensure (SYSPROF_TYPE_TASK_ROW);
}

static void
sysprof_window_init (SysprofWindow *self)
{
  g_autoptr(GPropertyAction) show_left_sidebar = NULL;
  g_autoptr(GPropertyAction) show_right_sidebar = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  sysprof_window_update_action_state (self);

  show_left_sidebar = g_property_action_new ("show-left-sidebar",
                                             self->left_split_overlay,
                                             "show-sidebar");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (show_left_sidebar));

  show_right_sidebar = g_property_action_new ("show-right-sidebar",
                                              self->right_split_overlay,
                                              "show-sidebar");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (show_right_sidebar));

  adw_window_title_set_title (self->stack_title, _("Loading…"));
}

GtkWidget *
sysprof_window_new (SysprofApplication *app,
                    SysprofDocument    *document)
{
  g_return_val_if_fail (SYSPROF_IS_APPLICATION (app), NULL);
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  return g_object_new (SYSPROF_TYPE_WINDOW,
                       "application", app,
                       "document", document,
                       NULL);
}

/**
 * sysprof_window_get_session:
 * @self: a #SysprofWindow
 *
 * Gets the session for the window.
 *
 * Returns: (transfer none): a #SysprofSession
 */
SysprofSession *
sysprof_window_get_session (SysprofWindow *self)
{
  g_return_val_if_fail (SYSPROF_IS_WINDOW (self), NULL);

  return self->session;
}

/**
 * sysprof_window_get_document:
 * @self: a #SysprofWindow
 *
 * Gets the document for the window.
 *
 * Returns: (transfer none): a #SysprofDocument
 */
SysprofDocument *
sysprof_window_get_document (SysprofWindow *self)
{
  g_return_val_if_fail (SYSPROF_IS_WINDOW (self), NULL);

  return self->document;
}

static void
sysprof_window_load_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  SysprofDocumentLoader *loader = (SysprofDocumentLoader *)object;
  g_autoptr(SysprofWindow) self = user_data;
  g_autoptr(SysprofDocument) document = NULL;
  SysprofApplication *app;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_WINDOW (self));

  app = SYSPROF_APPLICATION_DEFAULT;

  g_application_release (G_APPLICATION (app));

  _gtk_widget_hide_with_fade (GTK_WIDGET (self->progress_bar));

  g_binding_unbind (g_object_get_data (G_OBJECT (loader), "message-binding"));
  g_object_set_data (G_OBJECT (loader), "message-binding", NULL);

  if (g_set_object (&self->loader, NULL))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADER]);

  if (!(document = sysprof_document_loader_load_finish (loader, result, &error)))
    {
      GtkWidget *dialog;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS

      dialog = adw_message_dialog_new (NULL, _("Invalid Document"), NULL);
      adw_message_dialog_format_body (ADW_MESSAGE_DIALOG (dialog),
                                      _("The document could not be loaded. Please check that you have the correct capture file.\n\n%s"),
                                      error->message);
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog), "close", _("Close"));
      gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (dialog));
      gtk_window_present (GTK_WINDOW (dialog));

      G_GNUC_END_IGNORE_DEPRECATIONS

      return;
    }

  sysprof_window_set_document (self, document);
}

static void
sysprof_window_apply_loader_settings (SysprofDocumentLoader *loader)
{
  /* TODO: apply loader settings from gsettings/etc */
}

static SysprofWindow *
sysprof_window_create (SysprofApplication    *app,
                       SysprofDocumentLoader *loader)
{
  SysprofWindow *self;

  g_assert (SYSPROF_IS_APPLICATION (app));

  self = g_object_new (SYSPROF_TYPE_WINDOW,
                       "application", app,
                       NULL);
  self->loader = g_object_ref (loader);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADER]);
  g_object_bind_property (loader, "fraction",
                          self->progress_bar, "fraction",
                          G_BINDING_SYNC_CREATE);
  g_object_set_data (G_OBJECT (loader), "message-binding",
                     g_object_bind_property (loader, "message",
                                             self->stack_title, "subtitle",
                                             G_BINDING_SYNC_CREATE));

  return self;
}

void
sysprof_window_open (SysprofApplication       *app,
                     SysprofRecordingTemplate *template,
                     GFile                    *file)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofRecordingTemplate) alt_template = NULL;
  g_autoptr(GError) error = NULL;
  SysprofWindow *self;

  g_return_if_fail (SYSPROF_IS_APPLICATION (app));
  g_return_if_fail (!template || SYSPROF_IS_RECORDING_TEMPLATE (template));
  g_return_if_fail (G_IS_FILE (file));

  if (template == NULL)
    template = alt_template = sysprof_recording_template_new ();

  if (!g_file_is_native (file))
    {
      g_autofree char *uri = g_file_get_uri (file);
      g_warning ("Cannot open non-native file \"%s\"", uri);
      return;
    }

  if (!(loader = sysprof_recording_template_create_loader (template, file, &error)))
    {
      g_critical ("Failed to create loader: %s", error->message);
      return;
    }

  g_application_hold (G_APPLICATION (app));
  self = sysprof_window_create (app, loader);
  sysprof_document_loader_load_async (loader,
                                      NULL,
                                      sysprof_window_load_cb,
                                      g_object_ref (self));
  gtk_window_present (GTK_WINDOW (self));
}

void
sysprof_window_open_fd (SysprofApplication       *app,
                        SysprofRecordingTemplate *template,
                        int                       fd)
{
  g_autoptr(SysprofRecordingTemplate) alt_template = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(GError) error = NULL;
  SysprofWindow *self;

  g_return_if_fail (SYSPROF_IS_APPLICATION (app));
  g_return_if_fail (!template || SYSPROF_IS_RECORDING_TEMPLATE (template));
  g_return_if_fail (fd > -1);

  if (template == NULL)
    template = alt_template = sysprof_recording_template_new ();

  if (!(loader = sysprof_recording_template_create_loader_for_fd (template, fd, &error)))
    {
      g_critical ("Failed to create loader: %s", error->message);
      return;
    }

  g_debug ("Opening recording by FD");

  g_application_hold (G_APPLICATION (app));
  sysprof_window_apply_loader_settings (loader);
  self = sysprof_window_create (app, loader);
  sysprof_document_loader_load_async (loader,
                                      NULL,
                                      sysprof_window_load_cb,
                                      g_object_ref (self));
  gtk_window_present (GTK_WINDOW (self));
}
