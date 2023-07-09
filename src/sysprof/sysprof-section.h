/* sysprof-section.h
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

#pragma once

#include <gtk/gtk.h>

#include <sysprof-gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_SECTION (sysprof_section_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofSection, sysprof_section, SYSPROF, SECTION, GtkWidget)

struct _SysprofSectionClass
{
  GtkWidgetClass parent_class;
};

SysprofSession *sysprof_section_get_session (SysprofSection *self);
const char     *sysprof_section_get_title   (SysprofSection *self);
void            sysprof_section_set_title   (SysprofSection *self,
                                             const char     *title);

G_END_DECLS
