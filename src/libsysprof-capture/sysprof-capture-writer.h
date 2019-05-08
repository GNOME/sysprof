/* sysprof-capture-writer.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "sysprof-capture-types.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef struct _SysprofCaptureWriter SysprofCaptureWriter;

typedef struct
{
  /*
   * The number of frames indexed by SysprofCaptureFrameType
   */
  gsize frame_count[16];

  /*
   * Padding for future expansion.
   */
  gsize padding[48];
} SysprofCaptureStat;

SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter    *sysprof_capture_writer_new_from_env    (gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter    *sysprof_capture_writer_new             (const gchar             *filename,
                                                       gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter    *sysprof_capture_writer_new_from_fd     (int                      fd,
                                                       gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter    *sysprof_capture_writer_ref             (SysprofCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_capture_writer_unref           (SysprofCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
void                sysprof_capture_writer_stat            (SysprofCaptureWriter         *self,
                                                       SysprofCaptureStat           *stat);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_map         (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       guint64                  start,
                                                       guint64                  end,
                                                       guint64                  offset,
                                                       guint64                  inode,
                                                       const gchar             *filename);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_mark        (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       guint64                  duration,
                                                       const gchar             *group,
                                                       const gchar             *name,
                                                       const gchar             *message);
SYSPROF_AVAILABLE_IN_ALL
guint64             sysprof_capture_writer_add_jitmap      (SysprofCaptureWriter         *self,
                                                       const gchar             *name);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_process     (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const gchar             *cmdline);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_sample      (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       gint32                   tid,
                                                       const SysprofCaptureAddress  *addrs,
                                                       guint                    n_addrs);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_fork        (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       gint32                   child_pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_exit        (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_add_timestamp   (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_define_counters (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const SysprofCaptureCounter  *counters,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_set_counters    (SysprofCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const guint             *counters_ids,
                                                       const SysprofCaptureCounterValue *values,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_flush           (SysprofCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_save_as         (SysprofCaptureWriter         *self,
                                                       const gchar             *filename,
                                                       GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
guint               sysprof_capture_writer_request_counter (SysprofCaptureWriter         *self,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader    *sysprof_capture_writer_create_reader   (SysprofCaptureWriter         *self,
                                                       GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sysprof_capture_writer_splice          (SysprofCaptureWriter         *self,
                                                       SysprofCaptureWriter         *dest,
                                                       GError                 **error);
G_GNUC_INTERNAL
gboolean            _sysprof_capture_writer_splice_from_fd (SysprofCaptureWriter         *self,
                                                       int                      fd,
                                                       GError                 **error) G_GNUC_INTERNAL;
G_GNUC_INTERNAL
gboolean            _sysprof_capture_writer_set_time_range (SysprofCaptureWriter         *self,
                                                       gint64                   start_time,
                                                       gint64                   end_time) G_GNUC_INTERNAL;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

G_END_DECLS
