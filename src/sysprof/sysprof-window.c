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

#include <glib/gi18n.h>

#include <sysprof-gtk.h>

#include "sysprof-cpu-info-dialog.h"
#include "sysprof-files-section.h"
#include "sysprof-greeter.h"
#include "sysprof-logs-section.h"
#include "sysprof-marks-section.h"
#include "sysprof-memory-section.h"
#include "sysprof-metadata-section.h"
#include "sysprof-processes-section.h"
#include "sysprof-samples-section.h"
#include "sysprof-sidebar.h"
#include "sysprof-window.h"

struct _SysprofWindow
{
  AdwApplicationWindow  parent_instance;

  SysprofDocument      *document;
  SysprofSession       *session;

  GtkToggleButton      *show_right_sidebar;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
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
}

static void
show_greeter (SysprofWindow      *self,
              SysprofGreeterPage  page)
{
  SysprofGreeter *greeter;

  g_assert (SYSPROF_IS_WINDOW (self));

  greeter = g_object_new (SYSPROF_TYPE_GREETER,
                          "transient-for", self,
                          NULL);
  sysprof_greeter_set_page (greeter, page);
  gtk_window_present (GTK_WINDOW (greeter));
}

static void
sysprof_window_open_capture_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  show_greeter (SYSPROF_WINDOW (widget), SYSPROF_GREETER_PAGE_OPEN);
}

static void
sysprof_window_record_capture_action (GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *param)
{
  show_greeter (SYSPROF_WINDOW (widget), SYSPROF_GREETER_PAGE_RECORD);
}

static void
sysprof_window_show_cpu_info_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  GtkWindow *window;

  g_assert (SYSPROF_IS_WINDOW (self));

  window = g_object_new (SYSPROF_TYPE_CPU_INFO_DIALOG,
                         "document", self->document,
                         "transient-for", self,
                         NULL);

  gtk_window_present (window);
}

static void
sysprof_window_set_document (SysprofWindow   *self,
                             SysprofDocument *document)
{
  static const char *callgraph_actions[] = {
    "include-threads",
    "hide-system-libraries",
  };

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (self->document == NULL);
  g_assert (self->session == NULL);

  g_set_object (&self->document, document);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);

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
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);

  for (guint i = 0; i < G_N_ELEMENTS (callgraph_actions); i++)
    {
      g_autofree char *action_name = g_strdup_printf ("callgraph.%s", callgraph_actions[i]);
      g_autoptr(GPropertyAction) action = g_property_action_new (action_name, self->session, callgraph_actions[i]);

      g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (action));
    }

  sysprof_window_update_zoom_actions (self);
}

static void
main_view_notify_sidebar (SysprofWindow       *self,
                          GParamSpec          *pspec,
                          AdwOverlaySplitView *main_view)
{
  GtkWidget *sidebar;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (ADW_IS_OVERLAY_SPLIT_VIEW (main_view));

  sidebar = adw_overlay_split_view_get_sidebar (main_view);

  if (sidebar == NULL)
    adw_overlay_split_view_set_show_sidebar (main_view, FALSE);

  gtk_widget_set_sensitive (GTK_WIDGET (self->show_right_sidebar), sidebar != NULL);
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

  select.begin_nsec = visible_time->begin_nsec + (duration / 4);
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

  select.begin_nsec -= floor (duration / 2.);
  select.end_nsec += ceil (duration / 2.);

  sysprof_session_select_time (self->session, &select);
  sysprof_session_zoom_to_selection (self->session);
}

static void
sysprof_window_dispose (GObject *object)
{
  SysprofWindow *self = (SysprofWindow *)object;

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

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-window.ui");

  gtk_widget_class_bind_template_child (widget_class, SysprofWindow, show_right_sidebar);
  gtk_widget_class_bind_template_callback (widget_class, main_view_notify_sidebar);

  gtk_widget_class_install_action (widget_class, "win.open-capture", NULL, sysprof_window_open_capture_action);
  gtk_widget_class_install_action (widget_class, "win.record-capture", NULL, sysprof_window_record_capture_action);
  gtk_widget_class_install_action (widget_class, "win.show-cpu-info", NULL, sysprof_window_show_cpu_info_action);
  gtk_widget_class_install_action (widget_class, "session.zoom-one", NULL, sysprof_window_session_zoom_one);
  gtk_widget_class_install_action (widget_class, "session.zoom-out", NULL, sysprof_window_session_zoom_out);
  gtk_widget_class_install_action (widget_class, "session.zoom-in", NULL, sysprof_window_session_zoom_in);

  g_type_ensure (SYSPROF_TYPE_DOCUMENT);
  g_type_ensure (SYSPROF_TYPE_FILES_SECTION);
  g_type_ensure (SYSPROF_TYPE_LOGS_SECTION);
  g_type_ensure (SYSPROF_TYPE_MARKS_SECTION);
  g_type_ensure (SYSPROF_TYPE_MEMORY_SECTION);
  g_type_ensure (SYSPROF_TYPE_METADATA_SECTION);
  g_type_ensure (SYSPROF_TYPE_PROCESSES_SECTION);
  g_type_ensure (SYSPROF_TYPE_SAMPLES_SECTION);
  g_type_ensure (SYSPROF_TYPE_SESSION);
  g_type_ensure (SYSPROF_TYPE_SIDEBAR);
}

static void
sysprof_window_init (SysprofWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
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
  g_autoptr(SysprofApplication) app = user_data;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_APPLICATION (app));

  g_application_release (G_APPLICATION (app));

  if (!(document = sysprof_document_loader_load_finish (loader, result, &error)))
    {
      GtkWidget *dialog;

      dialog = adw_message_dialog_new (NULL, _("Invalid Document"), NULL);
      adw_message_dialog_format_body (ADW_MESSAGE_DIALOG (dialog),
                                      _("The document could not be loaded. Please check that you have the correct capture file.\n\n%s"),
                                      error->message);
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog), "close", _("Close"));
      gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (dialog));
      gtk_window_present (GTK_WINDOW (dialog));
    }
  else
    {
      GtkWidget *window;

      window = sysprof_window_new (app, document);
      gtk_window_present (GTK_WINDOW (window));
    }
}

static void
sysprof_window_apply_loader_settings (SysprofDocumentLoader *loader)
{
  /* TODO: apply loader settings from gsettings/etc */
}

void
sysprof_window_open (SysprofApplication *app,
                     GFile              *file)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;

  g_return_if_fail (SYSPROF_IS_APPLICATION (app));
  g_return_if_fail (G_IS_FILE (file));

  if (!g_file_is_native (file) ||
      !(loader = sysprof_document_loader_new (g_file_peek_path (file))))
    {
      g_autofree char *uri = g_file_get_uri (file);
      g_warning ("Cannot open non-native file \"%s\"", uri);
      return;
    }

  g_application_hold (G_APPLICATION (app));
  sysprof_window_apply_loader_settings (loader);
  sysprof_document_loader_load_async (loader,
                                      NULL,
                                      sysprof_window_load_cb,
                                      g_object_ref (app));

}

void
sysprof_window_open_fd (SysprofApplication *app,
                        int                 fd)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail (SYSPROF_IS_APPLICATION (app));

  if (!(loader = sysprof_document_loader_new_for_fd (fd, &error)))
    {
      g_critical ("Failed to dup FD: %s", error->message);
      return;
    }

  g_debug ("Opening recording by FD");

  g_application_hold (G_APPLICATION (app));
  sysprof_window_apply_loader_settings (loader);
  sysprof_document_loader_load_async (loader,
                                      NULL,
                                      sysprof_window_load_cb,
                                      g_object_ref (app));

}
