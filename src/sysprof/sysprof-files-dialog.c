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
