/* sysprof-profiler.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-profiler"

#include "config.h"

#include "sysprof-profiler.h"

G_DEFINE_INTERFACE (SysprofProfiler, sysprof_profiler, G_TYPE_OBJECT)

enum {
  FAILED,
  STOPPED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sysprof_profiler_default_init (SysprofProfilerInterface *iface)
{
  signals [FAILED] = g_signal_new ("failed",
                                   G_TYPE_FROM_INTERFACE (iface),
                                   G_SIGNAL_RUN_LAST,
                                   G_STRUCT_OFFSET (SysprofProfilerInterface, failed),
                                   NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_ERROR);

  signals [STOPPED] = g_signal_new ("stopped",
                                    G_TYPE_FROM_INTERFACE (iface),
                                    G_SIGNAL_RUN_LAST,
                                    G_STRUCT_OFFSET (SysprofProfilerInterface, stopped),
                                    NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);

  g_object_interface_install_property (iface,
      g_param_spec_double ("elapsed",
                           "Elapsed",
                           "The amount of elapsed time profiling",
                           0,
                           G_MAXDOUBLE,
                           0,
                           (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boolean ("is-running",
                            "Is Running",
                            "If the profiler is currently running.",
                            FALSE,
                            (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boolean ("is-mutable",
                            "Is Mutable",
                            "If the profiler can still be prepared.",
                            TRUE,
                            (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boolean ("spawn-inherit-environ",
                            "Sysprofawn Inherit Environ",
                            "If the spawned child should inherit the parents environment",
                            TRUE,
                            (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boolean ("whole-system",
                            "Whole System",
                            "If the whole system should be profiled",
                            TRUE,
                            (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boolean ("spawn",
                            "Sysprofawn",
                            "If configured child should be spawned",
                            TRUE,
                            (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("spawn-argv",
                          "Sysprofawn Argv",
                          "The arguments for the spawn child",
                          G_TYPE_STRV,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_string ("spawn-cwd",
                           "Spawn Working Directory",
                           "The directory to spawn the application from",
                           NULL,
                           (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("spawn-env",
                          "Sysprofawn Environment",
                          "The environment for the spawn child",
                          G_TYPE_STRV,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

gdouble
sysprof_profiler_get_elapsed (SysprofProfiler *self)
{
  gdouble value = 0.0;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), 0.0);
  g_object_get (self, "elapsed", &value, NULL);
  return value;
}

gboolean
sysprof_profiler_get_is_running (SysprofProfiler *self)
{
  gboolean is_running = FALSE;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), FALSE);
  g_object_get (self, "is-running", &is_running, NULL);
  return is_running;
}

gboolean
sysprof_profiler_get_is_mutable (SysprofProfiler *self)
{
  gboolean is_mutable = FALSE;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), FALSE);
  g_object_get (self, "is-mutable", &is_mutable, NULL);
  return is_mutable;
}

gboolean
sysprof_profiler_get_spawn_inherit_environ (SysprofProfiler *self)
{
  gboolean spawn_inherit_environ = FALSE;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), FALSE);
  g_object_get (self, "spawn-inherit-environ", &spawn_inherit_environ, NULL);
  return spawn_inherit_environ;
}

void
sysprof_profiler_set_spawn_inherit_environ (SysprofProfiler *self,
                                            gboolean         spawn_inherit_environ)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "spawn-inherit-environ", !!spawn_inherit_environ, NULL);
}

gboolean
sysprof_profiler_get_spawn (SysprofProfiler *self)
{
  gboolean spawn = FALSE;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), FALSE);
  g_object_get (self, "spawn", &spawn, NULL);
  return spawn;
}

void
sysprof_profiler_set_spawn_cwd (SysprofProfiler *self,
                                const gchar     *spawn_cwd)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "spawn-cwd", spawn_cwd, NULL);
}

void
sysprof_profiler_set_spawn (SysprofProfiler *self,
                            gboolean    spawn)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "spawn", !!spawn, NULL);
}

gboolean
sysprof_profiler_get_whole_system (SysprofProfiler *self)
{
  gboolean whole_system = FALSE;
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), FALSE);
  g_object_get (self, "whole-system", &whole_system, NULL);
  return whole_system;
}

void
sysprof_profiler_set_whole_system (SysprofProfiler *self,
                                   gboolean         whole_system)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "whole-system", !!whole_system, NULL);
}

void
sysprof_profiler_set_spawn_argv (SysprofProfiler     *self,
                                 const gchar * const *spawn_argv)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "spawn-argv", spawn_argv, NULL);
}

void
sysprof_profiler_set_spawn_env (SysprofProfiler     *self,
                                const gchar * const *spawn_env)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_object_set (self, "spawn-env", spawn_env, NULL);
}

void
sysprof_profiler_add_source (SysprofProfiler *self,
                             SysprofSource   *source)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_return_if_fail (SYSPROF_IS_SOURCE (source));

  SYSPROF_PROFILER_GET_IFACE (self)->add_source (self, source);
}

void
sysprof_profiler_set_writer (SysprofProfiler      *self,
                             SysprofCaptureWriter *writer)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_return_if_fail (writer != NULL);

  SYSPROF_PROFILER_GET_IFACE (self)->set_writer (self, writer);
}

SysprofCaptureWriter *
sysprof_profiler_get_writer (SysprofProfiler *self)
{
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), NULL);

  return SYSPROF_PROFILER_GET_IFACE (self)->get_writer (self);
}

void
sysprof_profiler_start (SysprofProfiler *self)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));

  SYSPROF_PROFILER_GET_IFACE (self)->start (self);
}

void
sysprof_profiler_stop (SysprofProfiler *self)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));

  SYSPROF_PROFILER_GET_IFACE (self)->stop (self);
}

void
sysprof_profiler_add_pid (SysprofProfiler *self,
                          GPid             pid)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_return_if_fail (pid > -1);

  SYSPROF_PROFILER_GET_IFACE (self)->add_pid (self, pid);
}

void
sysprof_profiler_remove_pid (SysprofProfiler *self,
                             GPid             pid)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_return_if_fail (pid > -1);

  SYSPROF_PROFILER_GET_IFACE (self)->remove_pid (self, pid);
}

const GPid *
sysprof_profiler_get_pids (SysprofProfiler *self,
                           guint           *n_pids)
{
  g_return_val_if_fail (SYSPROF_IS_PROFILER (self), NULL);
  g_return_val_if_fail (n_pids != NULL, NULL);

  return SYSPROF_PROFILER_GET_IFACE (self)->get_pids (self, n_pids);
}

void
sysprof_profiler_emit_failed (SysprofProfiler *self,
                              const GError    *error)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));
  g_return_if_fail (error != NULL);

  g_signal_emit (self, signals [FAILED], 0, error);
}

void
sysprof_profiler_emit_stopped (SysprofProfiler *self)
{
  g_return_if_fail (SYSPROF_IS_PROFILER (self));

  g_signal_emit (self, signals [STOPPED], 0);
}
