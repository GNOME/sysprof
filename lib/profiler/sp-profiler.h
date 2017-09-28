/* sp-profiler.h
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

#ifndef SP_PROFILER_H
#define SP_PROFILER_H

#include "capture/sp-capture-writer.h"
#include "sources/sp-source.h"

G_BEGIN_DECLS

#define SP_TYPE_PROFILER (sp_profiler_get_type())

G_DECLARE_INTERFACE (SpProfiler, sp_profiler, SP, PROFILER, GObject)

struct _SpProfilerInterface
{
  GTypeInterface parent_interface;

  /**
   * SpProfiler::failed:
   * @self: A #SpProfiler
   * @reason: A #GError representing the reason for the failure
   *
   * This signal is emitted if the profiler failed. Note that
   * #SpProfiler::stopped will also be emitted, but does not allow for
   * receiving the error condition.
   */
  void (*failed) (SpProfiler   *self,
                  const GError *error);

  /**
   * SpProfiler::stopped:
   * @self: A #SpProfiler
   *
   * This signal is emitted when a profiler is stopped. It will always be
   * emitted after a sp_profiler_start() has been called, either after
   * completion of sp_profiler_stop() or after a failure or after asynchronous
   * completion of stopping.
   */
  void (*stopped) (SpProfiler *self);

  /**
   * SpProfiler::add_source:
   *
   * Adds a source to the profiler.
   */
  void (*add_source) (SpProfiler *profiler,
                      SpSource   *source);

  /**
   * SpProfiler::set_writer:
   *
   * Sets the writer to use for the profiler.
   */
  void (*set_writer) (SpProfiler      *self,
                      SpCaptureWriter *writer);

  /**
   * SpProfiler::get_writer:
   *
   * Gets the writer that is being used to capture.
   *
   * Returns: (nullable) (transfer none): A #SpCaptureWriter.
   */
  SpCaptureWriter *(*get_writer) (SpProfiler *self);

  /**
   * SpProfiler::start:
   *
   * Starts the profiler.
   */
  void (*start) (SpProfiler *self);

  /**
   * SpProfiler::stop:
   *
   * Stops the profiler.
   */
  void (*stop) (SpProfiler *self);

  /**
   * SpProfiler::add_pid:
   *
   * Add a pid to be profiled.
   */
  void (*add_pid) (SpProfiler *self,
                   GPid        pid);

  /**
   * SpProfiler::remove_pid:
   *
   * Remove a pid from the profiler. This will not be called after
   * SpProfiler::start has been called.
   */
  void (*remove_pid) (SpProfiler *self,
                      GPid        pid);

  /**
   * SpProfiler::get_pids:
   *
   * Gets the pids that are part of this profiling session. If no pids
   * have been specified, %NULL is returned.
   *
   * Returns: (nullable) (transfer none): An array of #GPid, or %NULL.
   */
  const GPid *(*get_pids) (SpProfiler *self,
                           guint      *n_pids);
};

void             sp_profiler_emit_failed               (SpProfiler          *self,
                                                        const GError        *error);
void             sp_profiler_emit_stopped              (SpProfiler          *self);
gdouble          sp_profiler_get_elapsed               (SpProfiler          *self);
gboolean         sp_profiler_get_is_mutable            (SpProfiler          *self);
gboolean         sp_profiler_get_spawn_inherit_environ (SpProfiler          *self);
void             sp_profiler_set_spawn_inherit_environ (SpProfiler          *self,
                                                        gboolean             spawn_inherit_environ);
gboolean         sp_profiler_get_whole_system          (SpProfiler          *self);
void             sp_profiler_set_whole_system          (SpProfiler          *self,
                                                        gboolean             whole_system);
gboolean         sp_profiler_get_spawn                 (SpProfiler          *self);
void             sp_profiler_set_spawn                 (SpProfiler          *self,
                                                        gboolean             spawn);
void             sp_profiler_set_spawn_argv            (SpProfiler          *self,
                                                        const gchar * const *spawn_argv);
void             sp_profiler_set_spawn_env             (SpProfiler          *self,
                                                        const gchar * const *spawn_env);
void             sp_profiler_add_source                (SpProfiler          *self,
                                                        SpSource            *source);
void             sp_profiler_set_writer                (SpProfiler          *self,
                                                        SpCaptureWriter     *writer);
SpCaptureWriter *sp_profiler_get_writer                (SpProfiler          *self);
gboolean         sp_profiler_get_is_running            (SpProfiler          *self);
void             sp_profiler_start                     (SpProfiler          *self);
void             sp_profiler_stop                      (SpProfiler          *self);
void             sp_profiler_add_pid                   (SpProfiler          *self,
                                                        GPid                 pid);
void             sp_profiler_remove_pid                (SpProfiler          *self,
                                                        GPid                 pid);
const GPid      *sp_profiler_get_pids                  (SpProfiler          *self,
                                                        guint               *n_pids);

G_END_DECLS

#endif /* SP_PROFILER_H */
