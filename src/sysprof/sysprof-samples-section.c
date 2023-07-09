/* sysprof-samples-section.c
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

#include <sysprof-gtk.h>

#include "sysprof-samples-section.h"

struct _SysprofSamplesSection
{
  SysprofSection parent_instance;

  SysprofWeightedCallgraphView *callgraph_view;
};

G_DEFINE_FINAL_TYPE (SysprofSamplesSection, sysprof_samples_section, SYSPROF_TYPE_SECTION)

static void
sysprof_samples_section_dispose (GObject *object)
{
  SysprofSamplesSection *self = (SysprofSamplesSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_SAMPLES_SECTION);

  G_OBJECT_CLASS (sysprof_samples_section_parent_class)->dispose (object);
}

static void
sysprof_samples_section_class_init (SysprofSamplesSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_samples_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-samples-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofSamplesSection, callgraph_view);

  g_type_ensure (SYSPROF_TYPE_WEIGHTED_CALLGRAPH_VIEW);
}

static void
sysprof_samples_section_init (SysprofSamplesSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
