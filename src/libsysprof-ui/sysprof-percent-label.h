/* sysprof-percent-label.h
 *
 * Copyright 2023 Corentin Noël <corentin.noel@collabora.com>
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

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_PERCENT_LABEL (sysprof_percent_label_get_type())

G_DECLARE_FINAL_TYPE (SysprofPercentLabel, sysprof_percent_label, SYSPROF, PERCENT_LABEL, GtkWidget)

SysprofPercentLabel *sysprof_percent_label_new (void);
gdouble              sysprof_percent_label_get_percent (SysprofPercentLabel *self);
void                 sysprof_percent_label_set_percent (SysprofPercentLabel *self,
                                                        gdouble              percent);

G_END_DECLS
