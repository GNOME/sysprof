/*
 * sysprof-task-row.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <sysprof.h>

#include "sysprof-task-row.h"

struct _SysprofTaskRow
{
  GtkWidget            parent_instance;
  SysprofDocumentTask *task;
};

enum {
  PROP_0,
  PROP_TASK,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTaskRow, sysprof_task_row, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static void
sysprof_task_row_cancel (GtkWidget  *widget,
                         const char *action,
                         GVariant   *param)
{
  SysprofTaskRow *self = SYSPROF_TASK_ROW (widget);

  if (self->task)
    sysprof_document_task_cancel (self->task);
}

static void
sysprof_task_row_dispose (GObject *object)
{
  SysprofTaskRow *self = (SysprofTaskRow *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TASK_ROW);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->task);

  G_OBJECT_CLASS (sysprof_task_row_parent_class)->dispose (object);
}

static void
sysprof_task_row_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofTaskRow *self = SYSPROF_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_TASK:
      g_value_set_object (value, self->task);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_task_row_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SysprofTaskRow *self = SYSPROF_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_TASK:
      if (g_set_object (&self->task, g_value_get_object (value)))
        g_object_notify_by_pspec (object, pspec);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_task_row_class_init (SysprofTaskRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_task_row_dispose;
  object_class->get_property = sysprof_task_row_get_property;
  object_class->set_property = sysprof_task_row_set_property;

  properties[PROP_TASK] =
    g_param_spec_object ("task", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_TASK,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-task-row.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);

  gtk_widget_class_install_action (widget_class, "task.cancel", NULL, sysprof_task_row_cancel);
}

static void
sysprof_task_row_init (SysprofTaskRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
