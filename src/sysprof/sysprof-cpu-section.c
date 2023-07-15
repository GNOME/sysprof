/* sysprof-cpu-section.c
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

#include "sysprof-chart.h"
#include "sysprof-cpu-section.h"
#include "sysprof-line-layer.h"
#include "sysprof-time-filter-model.h"
#include "sysprof-time-scrubber.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofCpuSection
{
  SysprofSection       parent_instance;

  SysprofTimeScrubber *scrubber;
  GtkColumnView       *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofCpuSection, sysprof_cpu_section, SYSPROF_TYPE_SECTION)

static GtkWidget *
create_counter_chart (SysprofDocumentCounter *counter,
                      SysprofAxis            *x_axis)
{
  g_autoptr(SysprofValueAxis) y_axis = NULL;
  g_autoptr(SysprofSeries) series = NULL;
  SysprofChartLayer *layer;
  SysprofChart *chart;

  g_assert (SYSPROF_IS_DOCUMENT_COUNTER (counter));

  series = sysprof_xy_series_new (NULL,
                                  g_object_ref (G_LIST_MODEL (counter)),
                                  gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "time"),
                                  gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL, "value-double"));
  y_axis = g_object_new (SYSPROF_TYPE_VALUE_AXIS,
                         "min-value", 0.,
                         "max-value", 100.,
                         NULL);
  layer = g_object_new (SYSPROF_TYPE_LINE_LAYER,
                        "fill", TRUE,
                        "series", series,
                        "x-axis", x_axis,
                        "y-axis", y_axis,
                        "spline", TRUE,
                        NULL);
  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "height-request", 20,
                        NULL);
  sysprof_chart_add_layer (chart, layer);

  return GTK_WIDGET (chart);
}

static void
sysprof_cpu_section_session_set (SysprofSection *section,
                                 SysprofSession *session)
{
  SysprofCpuSection *self = (SysprofCpuSection *)section;
  g_autoptr(SysprofDocumentCounter) combined_counter = NULL;
  SysprofDocument *document;
  SysprofAxis *x_axis;

  g_assert (SYSPROF_IS_CPU_SECTION (self));
  g_assert (!session || SYSPROF_IS_SESSION (session));

  if (!session)
    return;

  document = sysprof_session_get_document (session);
  x_axis = sysprof_session_get_visible_time_axis (session);

  if ((combined_counter = sysprof_document_find_counter (document, "CPU Percent", "Combined")))
    {
      g_autoptr(SysprofTimeFilterModel) filter = sysprof_time_filter_model_new (NULL, NULL);

      sysprof_time_filter_model_set_model (filter, G_LIST_MODEL (combined_counter));
      g_object_bind_property (session, "visible-time", filter, "time-span", G_BINDING_SYNC_CREATE);

      sysprof_time_scrubber_add_chart (self->scrubber,
                                       create_counter_chart (combined_counter, x_axis));
    }

}

static void
sysprof_cpu_section_dispose (GObject *object)
{
  SysprofCpuSection *self = (SysprofCpuSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_CPU_SECTION);

  G_OBJECT_CLASS (sysprof_cpu_section_parent_class)->dispose (object);
}

static void
sysprof_cpu_section_class_init (SysprofCpuSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofSectionClass *section_class = SYSPROF_SECTION_CLASS (klass);

  object_class->dispose = sysprof_cpu_section_dispose;

  section_class->session_set = sysprof_cpu_section_session_set;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-cpu-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofCpuSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCpuSection, scrubber);
  gtk_widget_class_bind_template_child (widget_class, SysprofCpuSection, time_column);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_MARK);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_COUNTER);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE);
  g_type_ensure (SYSPROF_TYPE_LINE_LAYER);
  g_type_ensure (SYSPROF_TYPE_TIME_SERIES);
  g_type_ensure (SYSPROF_TYPE_TIME_SPAN_LAYER);
  g_type_ensure (SYSPROF_TYPE_XY_SERIES);
}

static void
sysprof_cpu_section_init (SysprofCpuSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view, self->time_column, GTK_SORT_ASCENDING);
}
