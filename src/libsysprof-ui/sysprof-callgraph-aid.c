/* sysprof-callgraph-aid.c
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

#define G_LOG_DOMAIN "sysprof-callgraph-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-callgraph-aid.h"

struct _SysprofCallgraphAid
{
  SysprofAid parent_instance;
};

G_DEFINE_TYPE (SysprofCallgraphAid, sysprof_callgraph_aid, SYSPROF_TYPE_AID)

/**
 * sysprof_callgraph_aid_new:
 *
 * Create a new #SysprofCallgraphAid.
 *
 * Returns: (transfer full): a newly created #SysprofCallgraphAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_callgraph_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_CALLGRAPH_AID, NULL);
}

static void
sysprof_callgraph_aid_prepare (SysprofAid      *self,
                               SysprofProfiler *profiler)
{
  g_assert (SYSPROF_IS_CALLGRAPH_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

#ifdef __linux__
  {
    const GPid *pids;
    guint n_pids;

    if ((pids = sysprof_profiler_get_pids (profiler, &n_pids)))
      {
        for (guint i = 0; i < n_pids; i++)
          {
            g_autoptr(SysprofSource) source = NULL;

            source = sysprof_perf_source_new ();
            sysprof_perf_source_set_target_pid (SYSPROF_PERF_SOURCE (source), pids[i]);
            sysprof_profiler_add_source (profiler, source);
          }
      }
    else
      {
        g_autoptr(SysprofSource) source = NULL;

        source = sysprof_perf_source_new ();
        sysprof_profiler_add_source (profiler, source);
      }
  }
#endif
}

static void
sysprof_callgraph_aid_class_init (SysprofCallgraphAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_callgraph_aid_prepare;
}

static void
sysprof_callgraph_aid_init (SysprofCallgraphAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("Callgraph"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof-symbolic");
}
