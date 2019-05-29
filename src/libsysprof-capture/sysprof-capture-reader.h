/* sysprof-capture-reader.h
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

typedef struct _SysprofCaptureReader SysprofCaptureReader;

SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader                   *sysprof_capture_reader_new                 (const gchar              *filename,
                                                                                    GError                  **error);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader                   *sysprof_capture_reader_new_from_fd         (int                       fd,
                                                                                    GError                  **error);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader                   *sysprof_capture_reader_copy                (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader                   *sysprof_capture_reader_ref                 (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
void                                    sysprof_capture_reader_unref               (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gint                                    sysprof_capture_reader_get_byte_order      (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar                            *sysprof_capture_reader_get_filename        (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar                            *sysprof_capture_reader_get_time            (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gint64                                  sysprof_capture_reader_get_start_time      (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gint64                                  sysprof_capture_reader_get_end_time        (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_skip                (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_peek_type           (SysprofCaptureReader     *self,
                                                                                    SysprofCaptureFrameType  *type);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_peek_frame          (SysprofCaptureReader     *self,
                                                                                    SysprofCaptureFrame      *frame);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureLog                *sysprof_capture_reader_read_log            (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureMap                *sysprof_capture_reader_read_map            (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureMark               *sysprof_capture_reader_read_mark           (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureMetadata           *sysprof_capture_reader_read_metadata       (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureExit               *sysprof_capture_reader_read_exit           (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureFork               *sysprof_capture_reader_read_fork           (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureTimestamp          *sysprof_capture_reader_read_timestamp      (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureProcess            *sysprof_capture_reader_read_process        (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureSample             *sysprof_capture_reader_read_sample         (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
GHashTable                             *sysprof_capture_reader_read_jitmap         (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureCounterDefine      *sysprof_capture_reader_read_counter_define (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureCounterSet         *sysprof_capture_reader_read_counter_set    (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureFileChunk          *sysprof_capture_reader_read_file           (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_reset               (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_splice              (SysprofCaptureReader     *self,
                                                                                    SysprofCaptureWriter     *dest,
                                                                                    GError                  **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_save_as             (SysprofCaptureReader     *self,
                                                                                    const gchar              *filename,
                                                                                    GError                  **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_get_stat            (SysprofCaptureReader     *self,
                                                                                    SysprofCaptureStat       *st_buf);
SYSPROF_AVAILABLE_IN_ALL
void                                    sysprof_capture_reader_set_stat            (SysprofCaptureReader     *self,
                                                                                    const SysprofCaptureStat *st_buf);
SYSPROF_AVAILABLE_IN_ALL
const SysprofCaptureFileChunk          *sysprof_capture_reader_find_file           (SysprofCaptureReader     *self,
                                                                                    const gchar              *path);
SYSPROF_AVAILABLE_IN_ALL
gchar                                 **sysprof_capture_reader_list_files          (SysprofCaptureReader     *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                                sysprof_capture_reader_read_file_fd        (SysprofCaptureReader     *self,
                                                                                    const gchar              *path,
                                                                                    gint                      fd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

G_END_DECLS
