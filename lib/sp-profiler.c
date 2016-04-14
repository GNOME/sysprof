/* sp-profiler.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include "sp-profiler.h"

G_DEFINE_INTERFACE (SpProfiler, sp_profiler, G_TYPE_OBJECT)

enum {
  FAILED,
  STOPPED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sp_profiler_default_init (SpProfilerInterface *iface)
{
  signals [FAILED] = g_signal_new ("failed",
                                   G_TYPE_FROM_INTERFACE (iface),
                                   G_SIGNAL_RUN_LAST,
                                   G_STRUCT_OFFSET (SpProfilerInterface, failed),
                                   NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_ERROR);

  signals [STOPPED] = g_signal_new ("stopped",
                                    G_TYPE_FROM_INTERFACE (iface),
                                    G_SIGNAL_RUN_LAST,
                                    G_STRUCT_OFFSET (SpProfilerInterface, stopped),
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
                            "Spawn Inherit Environ",
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
                            "Spawn",
                            "If configured child should be spawned",
                            TRUE,
                            (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("spawn-argv",
                          "Spawn Argv",
                          "The arguments for the spawn child",
                          G_TYPE_STRV,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("spawn-env",
                          "Spawn Environment",
                          "The environment for the spawn child",
                          G_TYPE_STRV,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

gdouble
sp_profiler_get_elapsed (SpProfiler *self)
{
  gdouble value = 0.0;
  g_return_val_if_fail (SP_IS_PROFILER (self), 0.0);
  g_object_get (self, "elapsed", &value, NULL);
  return value;
}

gboolean
sp_profiler_get_is_running (SpProfiler *self)
{
  gboolean is_running = FALSE;
  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);
  g_object_get (self, "is-running", &is_running, NULL);
  return is_running;
}

gboolean
sp_profiler_get_is_mutable (SpProfiler *self)
{
  gboolean is_mutable = FALSE;
  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);
  g_object_get (self, "is-mutable", &is_mutable, NULL);
  return is_mutable;
}

gboolean
sp_profiler_get_spawn_inherit_environ (SpProfiler *self)
{
  gboolean spawn_inherit_environ = FALSE;
  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);
  g_object_get (self, "spawn-inherit-environ", &spawn_inherit_environ, NULL);
  return spawn_inherit_environ;
}

void
sp_profiler_set_spawn_inherit_environ (SpProfiler *self,
                                       gboolean    spawn_inherit_environ)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_object_set (self, "spawn-inherit-environ", !!spawn_inherit_environ, NULL);
}

gboolean
sp_profiler_get_spawn (SpProfiler *self)
{
  gboolean spawn = FALSE;
  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);
  g_object_get (self, "spawn", &spawn, NULL);
  return spawn;
}

void
sp_profiler_set_spawn (SpProfiler *self,
                       gboolean    spawn)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_object_set (self, "spawn", !!spawn, NULL);
}

gboolean
sp_profiler_get_whole_system (SpProfiler *self)
{
  gboolean whole_system = FALSE;
  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);
  g_object_get (self, "whole-system", &whole_system, NULL);
  return whole_system;
}

void
sp_profiler_set_whole_system (SpProfiler *self,
                              gboolean    whole_system)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_object_set (self, "whole-system", !!whole_system, NULL);
}

void
sp_profiler_set_spawn_argv (SpProfiler          *self,
                            const gchar * const *spawn_argv)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_object_set (self, "spawn-argv", spawn_argv, NULL);
}

void
sp_profiler_set_spawn_env (SpProfiler          *self,
                           const gchar * const *spawn_env)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_object_set (self, "spawn-env", spawn_env, NULL);
}

void
sp_profiler_add_source (SpProfiler *self,
                        SpSource   *source)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (SP_IS_SOURCE (source));

  SP_PROFILER_GET_IFACE (self)->add_source (self, source);
}

void
sp_profiler_set_writer (SpProfiler      *self,
                        SpCaptureWriter *writer)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (writer != NULL);

  SP_PROFILER_GET_IFACE (self)->set_writer (self, writer);
}

SpCaptureWriter *
sp_profiler_get_writer (SpProfiler *self)
{
  g_return_val_if_fail (SP_IS_PROFILER (self), NULL);

  return SP_PROFILER_GET_IFACE (self)->get_writer (self);
}

void
sp_profiler_start (SpProfiler *self)
{
  g_return_if_fail (SP_IS_PROFILER (self));

  SP_PROFILER_GET_IFACE (self)->start (self);
}

void
sp_profiler_stop (SpProfiler *self)
{
  g_return_if_fail (SP_IS_PROFILER (self));

  SP_PROFILER_GET_IFACE (self)->stop (self);
}

void
sp_profiler_add_pid (SpProfiler *self,
                     GPid        pid)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (pid > -1);

  SP_PROFILER_GET_IFACE (self)->add_pid (self, pid);
}

void
sp_profiler_remove_pid (SpProfiler *self,
                        GPid        pid)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (pid > -1);

  SP_PROFILER_GET_IFACE (self)->remove_pid (self, pid);
}

const GPid *
sp_profiler_get_pids (SpProfiler *self,
                      guint      *n_pids)
{
  g_return_val_if_fail (SP_IS_PROFILER (self), NULL);
  g_return_val_if_fail (n_pids != NULL, NULL);

  return SP_PROFILER_GET_IFACE (self)->get_pids (self, n_pids);
}

void
sp_profiler_emit_failed (SpProfiler   *self,
                         const GError *error)
{
  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (error != NULL);

  g_signal_emit (self, signals [FAILED], 0, error);
}

void
sp_profiler_emit_stopped (SpProfiler *self)
{
  g_return_if_fail (SP_IS_PROFILER (self));

  g_signal_emit (self, signals [STOPPED], 0);
}
