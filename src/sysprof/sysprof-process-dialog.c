/* sysprof-process-dialog.c
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

#include "sysprof-process-dialog.h"

struct _SysprofProcessDialog
{
  AdwWindow parent_instance;
  SysprofDocumentProcess *process;
};

enum {
  PROP_0,
  PROP_PROCESS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofProcessDialog, sysprof_process_dialog, ADW_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static char *
format_address (gpointer       unused,
                SysprofAddress addr)
{
  return g_strdup_printf ("0x%"G_GINT64_MODIFIER"x", addr);
}

static void
sysprof_process_dialog_dispose (GObject *object)
{
  SysprofProcessDialog *self = (SysprofProcessDialog *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_PROCESS_DIALOG);

  g_clear_object (&self->process);

  G_OBJECT_CLASS (sysprof_process_dialog_parent_class)->dispose (object);
}

static void
sysprof_process_dialog_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofProcessDialog *self = SYSPROF_PROCESS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_PROCESS:
      g_value_set_object (value, sysprof_process_dialog_get_process (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_process_dialog_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofProcessDialog *self = SYSPROF_PROCESS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_PROCESS:
      sysprof_process_dialog_set_process (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_process_dialog_class_init (SysprofProcessDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_process_dialog_dispose;
  object_class->get_property = sysprof_process_dialog_get_property;
  object_class->set_property = sysprof_process_dialog_set_property;

  properties [PROP_PROCESS] =
    g_param_spec_object ("process", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_PROCESS,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-process-dialog.ui");
  gtk_widget_class_bind_template_callback (widget_class, format_address);

  g_type_ensure (SYSPROF_TYPE_THREAD_INFO);
}

static void
sysprof_process_dialog_init (SysprofProcessDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

SysprofDocumentProcess *
sysprof_process_dialog_get_process (SysprofProcessDialog *self)
{
  g_return_val_if_fail (SYSPROF_IS_PROCESS_DIALOG (self), NULL);

  return self->process;
}

void
sysprof_process_dialog_set_process (SysprofProcessDialog   *self,
                                    SysprofDocumentProcess *process)
{
  g_return_if_fail (SYSPROF_IS_PROCESS_DIALOG (self));
  g_return_if_fail (!process || SYSPROF_IS_DOCUMENT_PROCESS (process));

  if (g_set_object (&self->process, process))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS]);
}
