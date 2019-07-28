/* sysprof-profiler.h
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

#pragma once

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include "sysprof-version-macros.h"

#include "sysprof-capture-writer.h"
#include "sysprof-source.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROFILER (sysprof_profiler_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (SysprofProfiler, sysprof_profiler, SYSPROF, PROFILER, GObject)

struct _SysprofProfilerInterface
{
  GTypeInterface parent_interface;

  /**
   * SysprofProfiler::failed:
   * @self: A #SysprofProfiler
   * @reason: A #GError representing the reason for the failure
   *
   * This signal is emitted if the profiler failed. Note that
   * #SysprofProfiler::stopped will also be emitted, but does not allow for
   * receiving the error condition.
   */
  void (*failed) (SysprofProfiler   *self,
                  const GError *error);

  /**
   * SysprofProfiler::stopped:
   * @self: A #SysprofProfiler
   *
   * This signal is emitted when a profiler is stopped. It will always be
   * emitted after a sysprof_profiler_start() has been called, either after
   * completion of sysprof_profiler_stop() or after a failure or after asynchronous
   * completion of stopping.
   */
  void (*stopped) (SysprofProfiler *self);

  /**
   * SysprofProfiler::add_source:
   *
   * Adds a source to the profiler.
   */
  void (*add_source) (SysprofProfiler *profiler,
                      SysprofSource   *source);

  /**
   * SysprofProfiler::set_writer:
   *
   * Sets the writer to use for the profiler.
   */
  void (*set_writer) (SysprofProfiler      *self,
                      SysprofCaptureWriter *writer);

  /**
   * SysprofProfiler::get_writer:
   *
   * Gets the writer that is being used to capture.
   *
   * Returns: (nullable) (transfer none): A #SysprofCaptureWriter.
   */
  SysprofCaptureWriter *(*get_writer) (SysprofProfiler *self);

  /**
   * SysprofProfiler::start:
   *
   * Starts the profiler.
   */
  void (*start) (SysprofProfiler *self);

  /**
   * SysprofProfiler::stop:
   *
   * Stops the profiler.
   */
  void (*stop) (SysprofProfiler *self);

  /**
   * SysprofProfiler::add_pid:
   *
   * Add a pid to be profiled.
   */
  void (*add_pid) (SysprofProfiler *self,
                   GPid        pid);

  /**
   * SysprofProfiler::remove_pid:
   *
   * Remove a pid from the profiler. This will not be called after
   * SysprofProfiler::start has been called.
   */
  void (*remove_pid) (SysprofProfiler *self,
                      GPid        pid);

  /**
   * SysprofProfiler::get_pids:
   *
   * Gets the pids that are part of this profiling session. If no pids
   * have been specified, %NULL is returned.
   *
   * Returns: (nullable) (transfer none): An array of #GPid, or %NULL.
   */
  const GPid *(*get_pids) (SysprofProfiler *self,
                           guint      *n_pids);
};

SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_emit_failed               (SysprofProfiler      *self,
                                                                  const GError         *error);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_emit_stopped              (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
gdouble               sysprof_profiler_get_elapsed               (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_profiler_get_is_mutable            (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_profiler_get_spawn_inherit_environ (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_spawn_inherit_environ (SysprofProfiler      *self,
                                                                  gboolean              spawn_inherit_environ);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_profiler_get_whole_system          (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_whole_system          (SysprofProfiler      *self,
                                                                  gboolean              whole_system);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_profiler_get_spawn                 (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_spawn                 (SysprofProfiler      *self,
                                                                  gboolean              spawn);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_spawn_argv            (SysprofProfiler      *self,
                                                                  const gchar * const  *spawn_argv);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_spawn_env             (SysprofProfiler      *self,
                                                                  const gchar * const  *spawn_env);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_spawn_cwd             (SysprofProfiler      *self,
                                                                  const gchar          *cwd);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_add_source                (SysprofProfiler      *self,
                                                                  SysprofSource        *source);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_set_writer                (SysprofProfiler      *self,
                                                                  SysprofCaptureWriter *writer);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter *sysprof_profiler_get_writer                (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_profiler_get_is_running            (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_start                     (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_stop                      (SysprofProfiler      *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_add_pid                   (SysprofProfiler      *self,
                                                                  GPid                  pid);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_profiler_remove_pid                (SysprofProfiler      *self,
                                                                  GPid                  pid);
SYSPROF_AVAILABLE_IN_ALL
const GPid           *sysprof_profiler_get_pids                  (SysprofProfiler      *self,
                                                                  guint                *n_pids);

G_END_DECLS
