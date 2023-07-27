/* sysprof-energy-section.c
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
#include "sysprof-energy-section.h"
#include "sysprof-line-layer.h"
#include "sysprof-time-filter-model.h"
#include "sysprof-time-scrubber.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofEnergySection
{
  SysprofSection       parent_instance;

  SysprofTimeScrubber *scrubber;
  GtkColumnView       *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofEnergySection, sysprof_energy_section, SYSPROF_TYPE_SECTION)

static void
sysprof_energy_section_dispose (GObject *object)
{
  SysprofEnergySection *self = (SysprofEnergySection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_ENERGY_SECTION);

  G_OBJECT_CLASS (sysprof_energy_section_parent_class)->dispose (object);
}

static void
sysprof_energy_section_class_init (SysprofEnergySectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_energy_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-energy-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofEnergySection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofEnergySection, scrubber);
  gtk_widget_class_bind_template_child (widget_class, SysprofEnergySection, time_column);

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
sysprof_energy_section_init (SysprofEnergySection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view, self->time_column, GTK_SORT_ASCENDING);
}
