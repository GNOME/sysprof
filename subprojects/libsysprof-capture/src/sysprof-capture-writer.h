/* sysprof-capture-writer.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
 * irrevocable (except for failure to satisfy the conditions of this license)
 * patent license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer this software, where such license applies only to those
 * patent claims, already acquired or hereafter acquired, licensable by such
 * copyright holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders
 *     and non-copyrightable additions of contributors, in source or binary
 *     form) alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#pragma once

#include "sysprof-capture-types.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef struct _SysprofCaptureWriter SysprofCaptureWriter;

SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter *sysprof_capture_writer_new_from_env    (gsize                              buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter *sysprof_capture_writer_new             (const gchar                       *filename,
                                                              gsize                              buffer_size);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter *sysprof_capture_writer_new_from_fd     (int                                fd,
                                                              gsize                              buffer_size);
SYSPROF_AVAILABLE_IN_ALL
gsize                 sysprof_capture_writer_get_buffer_size (SysprofCaptureWriter              *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureWriter *sysprof_capture_writer_ref             (SysprofCaptureWriter              *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_capture_writer_unref           (SysprofCaptureWriter              *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_capture_writer_stat            (SysprofCaptureWriter              *self,
                                                              SysprofCaptureStat                *stat);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_capture_writer_set_flush_delay (SysprofCaptureWriter              *self,
                                                              GMainContext                      *main_context,
                                                              guint                              timeout_seconds);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_file        (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const gchar                       *path,
                                                              gboolean                           is_last,
                                                              const guint8                      *data,
                                                              gsize                              data_len);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_file_fd     (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const gchar                       *path,
                                                              gint                               fd);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_map         (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              guint64                            start,
                                                              guint64                            end,
                                                              guint64                            offset,
                                                              guint64                            inode,
                                                              const gchar                       *filename);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_mark        (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              guint64                            duration,
                                                              const gchar                       *group,
                                                              const gchar                       *name,
                                                              const gchar                       *message);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_metadata    (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const gchar                       *id,
                                                              const gchar                       *metadata,
                                                              gssize                             metadata_len);
SYSPROF_AVAILABLE_IN_ALL
guint64               sysprof_capture_writer_add_jitmap      (SysprofCaptureWriter              *self,
                                                              const gchar                       *name);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_process     (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const gchar                       *cmdline);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_sample      (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              gint32                             tid,
                                                              const SysprofCaptureAddress       *addrs,
                                                              guint                              n_addrs);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_fork        (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              gint32                             child_pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_exit        (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_timestamp   (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_define_counters (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const SysprofCaptureCounter       *counters,
                                                              guint                              n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_set_counters    (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              const guint                       *counters_ids,
                                                              const SysprofCaptureCounterValue  *values,
                                                              guint                              n_counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_add_log         (SysprofCaptureWriter              *self,
                                                              gint64                             time,
                                                              gint                               cpu,
                                                              gint32                             pid,
                                                              GLogLevelFlags                     severity,
                                                              const gchar                       *domain,
                                                              const gchar                       *message);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_flush           (SysprofCaptureWriter              *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_save_as         (SysprofCaptureWriter              *self,
                                                              const gchar                       *filename,
                                                              GError                           **error);
SYSPROF_AVAILABLE_IN_ALL
guint                 sysprof_capture_writer_request_counter (SysprofCaptureWriter              *self,
                                                              guint                              n_counters);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader *sysprof_capture_writer_create_reader   (SysprofCaptureWriter              *self,
                                                              GError                           **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_splice          (SysprofCaptureWriter              *self,
                                                              SysprofCaptureWriter              *dest,
                                                              GError                           **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_writer_cat             (SysprofCaptureWriter              *self,
                                                              SysprofCaptureReader              *reader,
                                                              GError                           **error);
G_GNUC_INTERNAL
gboolean              _sysprof_capture_writer_splice_from_fd (SysprofCaptureWriter              *self,
                                                              int                                fd,
                                                              GError                           **error) G_GNUC_INTERNAL;
G_GNUC_INTERNAL
gboolean              _sysprof_capture_writer_set_time_range (SysprofCaptureWriter              *self,
                                                              gint64                             start_time,
                                                              gint64                             end_time) G_GNUC_INTERNAL;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

G_END_DECLS
