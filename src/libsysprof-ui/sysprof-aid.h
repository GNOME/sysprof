/* sysprof-aid.h
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gio/gio.h>
#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_AID (sysprof_aid_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofAid, sysprof_aid, SYSPROF, AID, GObject)

struct _SysprofAidClass
{
  GObjectClass parent_class;

  /*< gpointer >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
const gchar *sysprof_aid_get_display_name (SysprofAid  *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_aid_set_display_name (SysprofAid  *self,
                                           const gchar *display_name);
SYSPROF_AVAILABLE_IN_ALL
GIcon       *sysprof_aid_get_icon         (SysprofAid  *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_aid_set_icon         (SysprofAid  *self,
                                           GIcon       *icon);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_aid_set_icon_name    (SysprofAid  *self,
                                           const gchar *icon_name);

G_END_DECLS
