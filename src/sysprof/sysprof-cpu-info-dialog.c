/* sysprof-cpu-info-dialog.c
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

#include "sysprof-cpu-info-dialog.h"

struct _SysprofCpuInfoDialog
{
  AdwWindow parent_instance;
  SysprofDocument *document;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCpuInfoDialog, sysprof_cpu_info_dialog, ADW_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
sysprof_cpu_info_dialog_dispose (GObject *object)
{
  SysprofCpuInfoDialog *self = (SysprofCpuInfoDialog *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_CPU_INFO_DIALOG);

  g_clear_object (&self->document);

  G_OBJECT_CLASS (sysprof_cpu_info_dialog_parent_class)->dispose (object);
}

static void
sysprof_cpu_info_dialog_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofCpuInfoDialog *self = SYSPROF_CPU_INFO_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_cpu_info_dialog_get_document (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_info_dialog_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofCpuInfoDialog *self = SYSPROF_CPU_INFO_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      sysprof_cpu_info_dialog_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_info_dialog_class_init (SysprofCpuInfoDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_cpu_info_dialog_dispose;
  object_class->get_property = sysprof_cpu_info_dialog_get_property;
  object_class->set_property = sysprof_cpu_info_dialog_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-cpu-info-dialog.ui");
}

static void
sysprof_cpu_info_dialog_init (SysprofCpuInfoDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

SysprofDocument *
sysprof_cpu_info_dialog_get_document (SysprofCpuInfoDialog *self)
{
  g_return_val_if_fail (SYSPROF_IS_CPU_INFO_DIALOG (self), NULL);

  return self->document;
}

void
sysprof_cpu_info_dialog_set_document (SysprofCpuInfoDialog *self,
                                      SysprofDocument      *document)
{
  g_return_if_fail (SYSPROF_IS_CPU_INFO_DIALOG (self));
  g_return_if_fail (!document || SYSPROF_IS_DOCUMENT (document));

  if (g_set_object (&self->document, document))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);
}

