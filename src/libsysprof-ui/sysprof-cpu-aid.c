/* sysprof-cpu-aid.c
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

#define G_LOG_DOMAIN "sysprof-cpu-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-cpu-aid.h"

struct _SysprofCpuAid
{
  SysprofAid parent_instance;
};

G_DEFINE_TYPE (SysprofCpuAid, sysprof_cpu_aid, SYSPROF_TYPE_AID)

/**
 * sysprof_cpu_aid_new:
 *
 * Create a new #SysprofCpuAid.
 *
 * Returns: (transfer full): a newly created #SysprofCpuAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_cpu_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_CPU_AID, NULL);
}

static void
sysprof_cpu_aid_prepare (SysprofAid      *self,
                         SysprofProfiler *profiler)
{
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_CPU_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_hostinfo_source_new ();
  sysprof_profiler_add_source (profiler, source);
}

static void
sysprof_cpu_aid_class_init (SysprofCpuAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_cpu_aid_prepare;
}

static void
sysprof_cpu_aid_init (SysprofCpuAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("CPU Usage"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof-symbolic");
}
