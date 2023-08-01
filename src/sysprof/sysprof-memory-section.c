/* sysprof-memory-section.c
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
#include "sysprof-column-layer.h"
#include "sysprof-memory-callgraph-view.h"
#include "sysprof-memory-section.h"
#include "sysprof-session-model-item.h"
#include "sysprof-session-model.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofMemorySection
{
  SysprofSection parent_instance;

  SysprofMemoryCallgraphView *callgraph_view;
};

G_DEFINE_FINAL_TYPE (SysprofMemorySection, sysprof_memory_section, SYSPROF_TYPE_SECTION)

static char *
format_number (gpointer unused,
               guint    number)
{
  if (number == 0)
    return NULL;
  return g_strdup_printf ("%'u", number);
}

static void
sysprof_memory_section_dispose (GObject *object)
{
  SysprofMemorySection *self = (SysprofMemorySection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_MEMORY_SECTION);

  G_OBJECT_CLASS (sysprof_memory_section_parent_class)->dispose (object);
}

static void
sysprof_memory_section_class_init (SysprofMemorySectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_memory_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-memory-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofMemorySection, callgraph_view);
  gtk_widget_class_bind_template_callback (widget_class, format_number);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_COLUMN_LAYER);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_ALLOCATION);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_TRACEABLE);
  g_type_ensure (SYSPROF_TYPE_MEMORY_CALLGRAPH_VIEW);
  g_type_ensure (SYSPROF_TYPE_VALUE_AXIS);
  g_type_ensure (SYSPROF_TYPE_XY_SERIES);
}

static void
sysprof_memory_section_init (SysprofMemorySection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
