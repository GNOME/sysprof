/* sysprof-processes-section.c
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
#include <sysprof-gtk.h>

#include "sysprof-processes-section.h"
#include "sysprof-single-model.h"

struct _SysprofProcessesSection
{
  SysprofSection  parent_instance;

  GtkListView    *list_view;
};

G_DEFINE_FINAL_TYPE (SysprofProcessesSection, sysprof_processes_section, SYSPROF_TYPE_SECTION)

static void
sysprof_processes_section_dispose (GObject *object)
{
  SysprofProcessesSection *self = (SysprofProcessesSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_PROCESSES_SECTION);

  G_OBJECT_CLASS (sysprof_processes_section_parent_class)->dispose (object);
}

static void
sysprof_processes_section_class_init (SysprofProcessesSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_processes_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-processes-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofProcessesSection, list_view);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_CHART_LAYER);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_PROCESS);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL_ITEM);
  g_type_ensure (SYSPROF_TYPE_SINGLE_MODEL);
  g_type_ensure (SYSPROF_TYPE_TIME_LABEL);
  g_type_ensure (SYSPROF_TYPE_TIME_SERIES);
  g_type_ensure (SYSPROF_TYPE_TIME_SPAN_LAYER);
  g_type_ensure (SYSPROF_TYPE_VALUE_AXIS);
}

static void
sysprof_processes_section_init (SysprofProcessesSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
