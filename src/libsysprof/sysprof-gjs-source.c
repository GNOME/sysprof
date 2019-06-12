/* sysprof-gjs-source.c
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

#define G_LOG_DOMAIN "sysprof-gjs-source"

#include "config.h"

#include "sysprof-gjs-source.h"

struct _SysprofGjsSource
{
  SysprofTracefdSource parent_instance;
};

static SysprofSourceInterface *parent_iface;

static void
sysprof_gjs_source_modify_spawn (SysprofSource    *source,
                                 SysprofSpawnable *spawnable)
{
  sysprof_spawnable_setenv (spawnable, "GJS_ENABLE_PROFILER", "1");
  parent_iface->modify_spawn (source, spawnable);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  parent_iface = g_type_interface_peek_parent (iface);

  iface->modify_spawn = sysprof_gjs_source_modify_spawn;
}

G_DEFINE_TYPE_WITH_CODE (SysprofGjsSource, sysprof_gjs_source, SYSPROF_TYPE_TRACEFD_SOURCE,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
sysprof_gjs_source_class_init (SysprofGjsSourceClass *klass)
{
}

static void
sysprof_gjs_source_init (SysprofGjsSource *self)
{
  sysprof_tracefd_source_set_envvar (SYSPROF_TRACEFD_SOURCE (self), "GJS_TRACE_FD");
}

SysprofSource *
sysprof_gjs_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_GJS_SOURCE, NULL);
}
