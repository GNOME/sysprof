/* sp-source.h
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_SOURCE_H
#define SP_SOURCE_H

#include <glib-object.h>

#include "sp-capture-writer.h"

G_BEGIN_DECLS

#define SP_TYPE_SOURCE (sp_source_get_type())

G_DECLARE_INTERFACE (SpSource, sp_source, SP, SOURCE, GObject)

struct _SpSourceInterface
{
  GTypeInterface parent_iface;

  /**
   * SpSource::get_is_ready:
   * @self: A SpSource.
   *
   * This function should return %TRUE if the source is ready to start
   * profiling. If the source is not ready until after sp_source_start() has
   * been called, use sp_source_emit_ready() to notify the profiler that the
   * source is ready for profiling.
   *
   * Returns: %TRUE if the source is ready to start profiling.
   */
  gboolean (*get_is_ready) (SpSource *self);

  /**
   * SpSource::set_writer:
   * @self: A #SpSource.
   * @writer: A #SpCaptureWriter
   *
   * Sets the #SpCaptureWriter to use when profiling. @writer is only safe to
   * use from the main thread. If you need to capture from a thread, you should
   * create a memory-based #SpCaptureWriter and then splice that into this
   * writer from the main thread when profiling completes.
   *
   * See sp_capture_writer_splice() for information on splicing writers.
   */
  void (*set_writer) (SpSource        *self,
                      SpCaptureWriter *writer);

  /**
   * SpSource::prepare:
   *
   * This function is called before profiling has started. The source should
   * prepare any pre-profiling setup here. It may perform this work
   * asynchronously, but must g_object_notify() the SpSource::is-ready
   * property once that asynchronous work has been performed. Until it
   * is ready, #SpSource::is-ready must return FALSE.
   */
  void (*prepare) (SpSource *self);

  /**
   * SpSource::add_pid:
   * @self: A #SpSource
   * @pid: A pid_t > -1
   *
   * This function is used to notify the #SpSource that a new process,
   * identified by @pid, should be profiled. By default, sources should
   * assume all processes, and only restrict to a given set of pids if
   * this function is called.
   */
  void (*add_pid) (SpSource *self,
                   GPid      pid);

  /**
   * SpSource::start:
   * @self: A #SpSource.
   *
   * Start profiling as configured.
   *
   * If a failure occurs while processing, the source should notify the
   * profiling session via sp_source_emit_failed() from the main thread.
   */
  void (*start) (SpSource *self);

  /**
   * SpSource::stop:
   * @self: A #SpSource.
   *
   * Stop capturing a profile. The source should immediately stop
   * profiling and perform any cleanup tasks required. If doing
   * off-main-thread capturing, this is a good time to splice your
   * capture into the capture file set with sp_source_set_writer().
   *
   * If you need to perform asynchronous cleanup, call
   * sp_source_emit_finished() once that work has completed. If you do
   * not need to perform asynchronous cleanup, call
   * sp_source_emit_finished() from this function.
   *
   * sp_source_emit_finished() must be called from the main-thread.
   */
  void (*stop) (SpSource *self);
};

void     sp_source_add_pid       (SpSource        *self,
                                  GPid             pid);
void     sp_source_emit_ready    (SpSource        *self);
void     sp_source_emit_finished (SpSource        *self);
void     sp_source_emit_failed   (SpSource        *self,
                                  const GError    *error);
gboolean sp_source_get_is_ready  (SpSource        *self);
void     sp_source_prepare       (SpSource        *self);
void     sp_source_set_writer    (SpSource        *self,
                                  SpCaptureWriter *writer);
void     sp_source_start         (SpSource        *self);
void     sp_source_stop          (SpSource        *self);

G_END_DECLS

#endif /* SP_SOURCE_H */
