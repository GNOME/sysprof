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

#include "sysprof-files-dialog.h"
#include "sysprof-window.h"

struct _SysprofWindow
{
  AdwApplicationWindow  parent_instance;
  SysprofDocument      *document;
  SysprofSession       *session;
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
sysprof_window_open_capture_cb (GObject      *object,
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

  if ((file = gtk_file_dialog_open_finish (dialog, result, &error)))
    sysprof_window_open (SYSPROF_APPLICATION_DEFAULT, file);
}

static void
sysprof_window_open_capture_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GtkFileFilter) filter = NULL;
  g_autoptr(GListStore) filters = NULL;

  g_assert (SYSPROF_IS_WINDOW (widget));

  filter = gtk_file_filter_new ();
  gtk_file_filter_add_pattern (filter, "*.syscap");
  gtk_file_filter_add_mime_type (filter, "application/x-sysprof-capture");
  gtk_file_filter_set_name (filter, _("Sysprof Capture (*.syscap)"));

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Open Recording"));
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_set_default_filter (dialog, filter);

  gtk_file_dialog_open (dialog,
                        GTK_WINDOW (widget),
                        NULL,
                        sysprof_window_open_capture_cb,
                        g_object_ref (widget));
}

static void
sysprof_window_show_embedded_files_action (GtkWidget  *widget,
                                           const char *action_name,
                                           GVariant   *param)
{
  SysprofWindow *self = (SysprofWindow *)widget;
  GtkWidget *dialog;

  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_DOCUMENT (self->document));

  dialog = sysprof_files_dialog_new (self->document);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (self));
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
sysprof_window_set_document (SysprofWindow   *self,
                             SysprofDocument *document)
{
  g_assert (SYSPROF_IS_WINDOW (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (self->document == NULL);
  g_assert (self->session == NULL);

  g_set_object (&self->document, document);

  self->session = sysprof_session_new (document);
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

  gtk_widget_class_install_action (widget_class, "win.open-capture", NULL, sysprof_window_open_capture_action);
  gtk_widget_class_install_action (widget_class, "capture.show-embedded-files", NULL, sysprof_window_show_embedded_files_action);

  g_type_ensure (SYSPROF_TYPE_DOCUMENT);
  g_type_ensure (SYSPROF_TYPE_SESSION);
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
