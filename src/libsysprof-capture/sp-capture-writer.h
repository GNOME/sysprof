/* sp-capture-writer.h
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

#include "sp-capture-types.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef struct _SpCaptureWriter SpCaptureWriter;

typedef struct
{
  /*
   * The number of frames indexed by SpCaptureFrameType
   */
  gsize frame_count[16];

  /*
   * Padding for future expansion.
   */
  gsize padding[48];
} SpCaptureStat;

SYSPROF_AVAILABLE_IN_ALL
SpCaptureWriter    *sp_capture_writer_new_from_env    (gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureWriter    *sp_capture_writer_new             (const gchar             *filename,
                                                       gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureWriter    *sp_capture_writer_new_from_fd     (int                      fd,
                                                       gsize                    buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureWriter    *sp_capture_writer_ref             (SpCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
void                sp_capture_writer_unref           (SpCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
void                sp_capture_writer_stat            (SpCaptureWriter         *self,
                                                       SpCaptureStat           *stat);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_map         (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       guint64                  start,
                                                       guint64                  end,
                                                       guint64                  offset,
                                                       guint64                  inode,
                                                       const gchar             *filename);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_mark        (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       guint64                  duration,
                                                       const gchar             *group,
                                                       const gchar             *name,
                                                       const gchar             *message);
SYSPROF_AVAILABLE_IN_ALL
guint64             sp_capture_writer_add_jitmap      (SpCaptureWriter         *self,
                                                       const gchar             *name);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_process     (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const gchar             *cmdline);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_sample      (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       gint32                   tid,
                                                       const SpCaptureAddress  *addrs,
                                                       guint                    n_addrs);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_fork        (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       gint32                   child_pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_exit        (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_add_timestamp   (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_define_counters (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const SpCaptureCounter  *counters,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_set_counters    (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       gint32                   pid,
                                                       const guint             *counters_ids,
                                                       const SpCaptureCounterValue *values,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_flush           (SpCaptureWriter         *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_save_as         (SpCaptureWriter         *self,
                                                       const gchar             *filename,
                                                       GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
guint               sp_capture_writer_request_counter (SpCaptureWriter         *self,
                                                       guint                    n_counters);
SYSPROF_AVAILABLE_IN_ALL
SpCaptureReader    *sp_capture_writer_create_reader   (SpCaptureWriter         *self,
                                                       GError                 **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean            sp_capture_writer_splice          (SpCaptureWriter         *self,
                                                       SpCaptureWriter         *dest,
                                                       GError                 **error);
G_GNUC_INTERNAL
gboolean            _sp_capture_writer_splice_from_fd (SpCaptureWriter         *self,
                                                       int                      fd,
                                                       GError                 **error) G_GNUC_INTERNAL;
G_GNUC_INTERNAL
gboolean            _sp_capture_writer_set_time_range (SpCaptureWriter         *self,
                                                       gint64                   start_time,
                                                       gint64                   end_time) G_GNUC_INTERNAL;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpCaptureWriter, sp_capture_writer_unref)

G_END_DECLS
