/* sysprof-files-section.c
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

#include <adwaita.h>

#include "sysprof-files-section.h"
#include "sysprof-time-label.h"

struct _SysprofFilesSection
{
  SysprofSection  parent_instance;

  GtkColumnView  *column_view;
  GtkColumnViewColumn *path_column;
};

G_DEFINE_FINAL_TYPE (SysprofFilesSection, sysprof_files_section, SYSPROF_TYPE_SECTION)

static void
sysprof_files_section_activate_cb (SysprofFilesSection *self,
                                   guint                position,
                                   GtkColumnView       *view)
{
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GtkTextBuffer) buffer = NULL;
  g_autoptr(GBytes) bytes = NULL;
  GtkSelectionModel *model;
  GtkNative *toplevel;
  GtkWidget *toolbar;
  GtkWidget *header_bar;
  GtkWidget *scroller;
  GtkWidget *text_view;
  const char *str;
  const char *endptr;
  GtkWindow *window;
  gsize len;

  g_assert (SYSPROF_IS_FILES_SECTION (self));
  g_assert (GTK_IS_COLUMN_VIEW (view));

  model = gtk_column_view_get_model (view);
  file = g_list_model_get_item (G_LIST_MODEL (model), position);
  bytes = sysprof_document_file_dup_bytes (file);
  str = (const char *)g_bytes_get_data (bytes, &len);
  endptr = str;

  if (!g_utf8_validate_len (str, len, &endptr) && endptr == str)
    return;

  buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_set_text (buffer, str, endptr - str);

  toplevel = gtk_widget_get_native (GTK_WIDGET (self));

  window = g_object_new (ADW_TYPE_WINDOW,
                         "transient-for", toplevel,
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

static char *
format_size (gpointer unused,
             guint64  size)
{
  return g_format_size (size);
}

static void
sysprof_files_section_dispose (GObject *object)
{
  SysprofFilesSection *self = (SysprofFilesSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_FILES_SECTION);

  G_OBJECT_CLASS (sysprof_files_section_parent_class)->dispose (object);
}

static void
sysprof_files_section_class_init (SysprofFilesSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_files_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-files-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofFilesSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofFilesSection, path_column);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_files_section_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_size);

  g_type_ensure (SYSPROF_TYPE_DOCUMENT_FILE);
  g_type_ensure (SYSPROF_TYPE_TIME_LABEL);
}

static void
sysprof_files_section_init (SysprofFilesSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view, self->path_column, GTK_SORT_ASCENDING);
}
