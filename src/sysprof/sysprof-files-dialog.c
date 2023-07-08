/* sysprof-files-dialog.c
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

#include "sysprof-files-dialog.h"

struct _SysprofFilesDialog
{
  AdwWindow        parent_instance;

  SysprofDocument *document;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofFilesDialog, sysprof_files_dialog, ADW_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
sysprof_files_dialog_activate_cb (SysprofFilesDialog *self,
                                  guint               position,
                                  GtkListView        *list_view)
{
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GtkTextBuffer) buffer = NULL;
  g_autoptr(GBytes) bytes = NULL;
  GtkSelectionModel *model;
  GtkWidget *toolbar;
  GtkWidget *header_bar;
  GtkWidget *scroller;
  GtkWidget *text_view;
  const char *str;
  const char *endptr;
  GtkWindow *window;
  gsize len;

  g_assert (SYSPROF_IS_FILES_DIALOG (self));

  model = gtk_list_view_get_model (list_view);
  file = g_list_model_get_item (G_LIST_MODEL (model), position);
  bytes = sysprof_document_file_dup_bytes (file);
  str = (const char *)g_bytes_get_data (bytes, &len);
  endptr = str;

  if (!g_utf8_validate_len (str, len, &endptr) && endptr == str)
    return;

  buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_set_text (buffer, str, endptr - str);

  window = g_object_new (ADW_TYPE_WINDOW,
                         "transient-for", self,
                         "default-width", 800,
                         "default-height", 600,
                         "title", sysprof_document_file_get_path (file),
                         NULL);
  header_bar = g_object_new (ADW_TYPE_HEADER_BAR, NULL);
  toolbar = g_object_new (ADW_TYPE_TOOLBAR_VIEW,
                          "top-bar-style", ADW_TOOLBAR_RAISED,
                          NULL);
  scroller = g_object_new (GTK_TYPE_SCROLLED_WINDOW, NULL);
  text_view = g_object_new (GTK_TYPE_TEXT_VIEW,
                            "editable", FALSE,
                            "left-margin", 6,
                            "right-margin", 6,
                            "top-margin", 6,
                            "bottom-margin", 6,
                            "monospace", TRUE,
                            "buffer", buffer,
                            NULL);

  adw_window_set_content (ADW_WINDOW (window), toolbar);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header_bar);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scroller);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), text_view);

  gtk_window_present (window);
}

static void
sysprof_files_dialog_dispose (GObject *object)
{
  SysprofFilesDialog *self = (SysprofFilesDialog *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_FILES_DIALOG);

  g_clear_object (&self->document);

  G_OBJECT_CLASS (sysprof_files_dialog_parent_class)->dispose (object);
}

static void
sysprof_files_dialog_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofFilesDialog *self = SYSPROF_FILES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_files_dialog_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofFilesDialog *self = SYSPROF_FILES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      self->document = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_files_dialog_class_init (SysprofFilesDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_files_dialog_dispose;
  object_class->get_property = sysprof_files_dialog_get_property;
  object_class->set_property = sysprof_files_dialog_set_property;

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                        SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-files-dialog.ui");
  gtk_widget_class_bind_template_callback (widget_class, sysprof_files_dialog_activate_cb);
}

static void
sysprof_files_dialog_init (SysprofFilesDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_files_dialog_new (SysprofDocument *document)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  return g_object_new (SYSPROF_TYPE_FILES_DIALOG,
                       "document", document,
                       NULL);
}
