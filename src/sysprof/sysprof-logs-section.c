/* sysprof-logs-section.c
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

#include "sysprof-logs-section.h"

struct _SysprofLogsSection
{
  SysprofSection       parent_instance;

  GtkColumnView       *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofLogsSection, sysprof_logs_section, SYSPROF_TYPE_SECTION)

static char *
format_severity (gpointer       unused,
                 GLogLevelFlags severity)
{
  if (severity & G_LOG_LEVEL_CRITICAL)
    return g_strdup (_("Critical"));

  if (severity & G_LOG_LEVEL_WARNING)
    return g_strdup (_("Warning"));

  if (severity & G_LOG_LEVEL_DEBUG)
    return g_strdup (_("Debug"));

  if (severity & G_LOG_LEVEL_MESSAGE)
    return g_strdup (_("Message"));

  if (severity & G_LOG_LEVEL_INFO)
    return g_strdup (_("Info"));

  if (severity & G_LOG_LEVEL_ERROR)
    return g_strdup (_("Error"));

  return g_strdup ("");
}

static void
sysprof_logs_section_dispose (GObject *object)
{
  SysprofLogsSection *self = (SysprofLogsSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_LOGS_SECTION);

  G_OBJECT_CLASS (sysprof_logs_section_parent_class)->dispose (object);
}

static void
sysprof_logs_section_class_init (SysprofLogsSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_logs_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-logs-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofLogsSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofLogsSection, time_column);
  gtk_widget_class_bind_template_callback (widget_class, format_severity);

  g_type_ensure (SYSPROF_TYPE_DOCUMENT_LOG);
  g_type_ensure (SYSPROF_TYPE_TIME_LABEL);
}

static void
sysprof_logs_section_init (SysprofLogsSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view,
                                  self->time_column,
                                  GTK_SORT_ASCENDING);
}
