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

#include "sysprof-chart-layer.h"
#include "sysprof-chart.h"
#include "sysprof-document-process.h"
#include "sysprof-process-dialog.h"
#include "sysprof-processes-section.h"
#include "sysprof-session-model-item.h"
#include "sysprof-session-model.h"
#include "sysprof-single-model.h"
#include "sysprof-time-label.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"

struct _SysprofProcessesSection
{
  SysprofSection       parent_instance;

  GtkListView         *list_view;
  GtkColumnView       *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofProcessesSection, sysprof_processes_section, SYSPROF_TYPE_SECTION)

static char *
format_number (gpointer unused,
               guint    number)
{
  if (number == 0)
    return NULL;
  return g_strdup_printf ("%'u", number);
}

static void
show_dialog (GtkNative              *transient_for,
             SysprofDocumentProcess *process)
{
  SysprofProcessDialog *dialog;
  g_autofree char *title = NULL;

  g_assert (!transient_for || GTK_IS_NATIVE (transient_for));
  g_assert (SYSPROF_IS_DOCUMENT_PROCESS (process));

  title = sysprof_document_process_dup_title (process);

  dialog = g_object_new (SYSPROF_TYPE_PROCESS_DIALOG,
                         "process", process,
                         "transient-for", transient_for,
                         "title", title,
                         NULL);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
activate_layer_item_cb (GtkListItem            *list_item,
                        SysprofChartLayer      *layer,
                        SysprofDocumentProcess *process,
                        SysprofChart           *chart)
{

  GtkNative *transient_for;

  g_assert (GTK_IS_LIST_ITEM (list_item));
  g_assert (SYSPROF_IS_CHART_LAYER (layer));
  g_assert (SYSPROF_IS_DOCUMENT_PROCESS (process));
  g_assert (SYSPROF_IS_CHART (chart));

  transient_for = gtk_widget_get_native (GTK_WIDGET (chart));

  show_dialog (transient_for, process);
}

static void
process_table_activate_cb (SysprofProcessesSection *self,
                           guint                    position,
                           GtkColumnView           *column_view)
{
  g_autoptr(SysprofDocumentProcess) process = NULL;
  GtkSelectionModel *model;
  GtkNative *transient_for;

  g_assert (SYSPROF_IS_PROCESSES_SECTION (self));
  g_assert (GTK_IS_COLUMN_VIEW (column_view));

  model = gtk_column_view_get_model (column_view);
  process = g_list_model_get_item (G_LIST_MODEL (model), position);
  transient_for = gtk_widget_get_native (GTK_WIDGET (self));

  show_dialog (transient_for, process);
}

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
  gtk_widget_class_bind_template_child (widget_class, SysprofProcessesSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofProcessesSection, list_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofProcessesSection, time_column);
  gtk_widget_class_bind_template_callback (widget_class, activate_layer_item_cb);
  gtk_widget_class_bind_template_callback (widget_class, process_table_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_number);

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

  gtk_column_view_sort_by_column (self->column_view,
                                  self->time_column,
                                  GTK_SORT_ASCENDING);
}
