/* sysprof-aid-icon.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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
#include <sysprof.h>

#include "sysprof-aid.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_AID_ICON (sysprof_aid_icon_get_type())

G_DECLARE_FINAL_TYPE (SysprofAidIcon, sysprof_aid_icon, SYSPROF, AID_ICON, GtkFlowBoxChild)

GtkWidget  *sysprof_aid_icon_new         (SysprofAid     *aid);
SysprofAid *sysprof_aid_icon_get_aid     (SysprofAidIcon *self);
void        sysprof_aid_icon_toggle      (SysprofAidIcon *self);
gboolean    sysprof_aid_icon_is_selected (SysprofAidIcon *self);

G_END_DECLS
