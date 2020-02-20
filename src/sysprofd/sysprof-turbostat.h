/* sysprof-turbostat.h
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

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SysprofTurbostat SysprofTurbostat;

typedef struct
{
  gint    core;
  gint    cpu;
  gdouble pkg_watt;
  gdouble core_watt;
  gdouble gfx_watt;
  gdouble ram_watt;
} SysprofTurbostatSample;

SysprofTurbostat *sysprof_turbostat_new    (GFunc              sample_func,
                                            gpointer           sample_data);
gboolean          sysprof_turbostat_start  (SysprofTurbostat  *self,
                                            GError           **error);
void              sysprof_turbostat_stop   (SysprofTurbostat  *self);
gboolean          sysprof_turbostat_sample (SysprofTurbostat  *self,
                                            GError           **error);
SysprofTurbostat *sysprof_turbostat_ref    (SysprofTurbostat  *self);
void              sysprof_turbostat_unref  (SysprofTurbostat  *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofTurbostat, sysprof_turbostat_unref)

G_END_DECLS
