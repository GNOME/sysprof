/* sysprof-kallsyms.h
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SysprofKallsyms SysprofKallsyms;

SysprofKallsyms *sysprof_kallsyms_new       (const gchar      *path);
SysprofKallsyms *sysprof_kallsyms_new_take  (gchar            *data);
gboolean         sysprof_kallsyms_next      (SysprofKallsyms  *self,
                                             const gchar     **name,
                                             guint64          *address,
                                             guint8           *type);
void             sysprof_kallsyms_free      (SysprofKallsyms  *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofKallsyms, sysprof_kallsyms_free)

G_END_DECLS
