/* sysprof-memory-aid.c
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

#define G_LOG_DOMAIN "sysprof-memory-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-memory-aid.h"

struct _SysprofMemoryAid
{
  SysprofAid parent_instance;
};

G_DEFINE_TYPE (SysprofMemoryAid, sysprof_memory_aid, SYSPROF_TYPE_AID)

SysprofAid *
sysprof_memory_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_MEMORY_AID, NULL);
}

static void
sysprof_memory_aid_prepare (SysprofAid      *self,
                            SysprofProfiler *profiler)
{
#ifdef __linux__
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_MEMORY_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_memory_source_new ();
  sysprof_profiler_add_source (profiler, source);
#endif
}

static void
sysprof_memory_aid_class_init (SysprofMemoryAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_memory_aid_prepare;
}

static void
sysprof_memory_aid_init (SysprofMemoryAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Memory Usage"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof-symbolic");
}
