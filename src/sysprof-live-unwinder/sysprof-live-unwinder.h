/*
 * sysprof-live-unwinder.h
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_LIVE_UNWINDER (sysprof_live_unwinder_get_type())

G_DECLARE_FINAL_TYPE (SysprofLiveUnwinder, sysprof_live_unwinder, SYSPROF, LIVE_UNWINDER, GObject)

SysprofLiveUnwinder *sysprof_live_unwinder_new                        (SysprofCaptureWriter        *writer,
                                                                       int                          kallsyms_fd);
void                 sysprof_live_unwinder_seen_process               (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         pid,
                                                                       const char                  *comm);
void                 sysprof_live_unwinder_process_exited             (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         pid);
void                 sysprof_live_unwinder_process_forked             (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         parent_tid,
                                                                       GPid                         tid);
void                 sysprof_live_unwinder_track_mmap                 (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         pid,
                                                                       SysprofCaptureAddress        begin,
                                                                       SysprofCaptureAddress        end,
                                                                       SysprofCaptureAddress        offset,
                                                                       guint64                      inode,
                                                                       const char                  *filename,
                                                                       const char                  *build_id);
void                 sysprof_live_unwinder_process_sampled            (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         pid,
                                                                       GPid                         tid,
                                                                       const SysprofCaptureAddress *addresses,
                                                                       guint                        n_addresses);
void                 sysprof_live_unwinder_process_sampled_with_stack (SysprofLiveUnwinder         *self,
                                                                       gint64                       time,
                                                                       int                          cpu,
                                                                       GPid                         pid,
                                                                       GPid                         tid,
                                                                       const SysprofCaptureAddress *addresses,
                                                                       guint                        n_addresses,
                                                                       const guint8                *stack,
                                                                       guint64                      stack_size,
                                                                       guint64                      stack_dyn_size,
                                                                       guint64                      abi,
                                                                       const guint64               *registers,
                                                                       guint                        n_registers);

G_END_DECLS
