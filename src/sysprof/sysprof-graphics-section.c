/* sysprof-graphics-section.c
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

#include "sysprof-graphics-section.h"

struct _SysprofGraphicsSection
{
  SysprofSection parent_instance;

  GtkColumnView *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofGraphicsSection, sysprof_graphics_section, SYSPROF_TYPE_SECTION)

static void
sysprof_graphics_section_class_init (SysprofGraphicsSectionClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-graphics-section.ui");

  gtk_widget_class_bind_template_child (widget_class, SysprofGraphicsSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofGraphicsSection, time_column);
}

static void
sysprof_graphics_section_init (SysprofGraphicsSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view,
                                  self->time_column,
                                  GTK_SORT_ASCENDING);
}

