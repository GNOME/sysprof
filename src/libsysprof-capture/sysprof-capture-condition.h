/* sysprof-capture-condition.h
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

SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_copy                   (const SysprofCaptureCondition *self);
SYSPROF_AVAILABLE_IN_ALL
void                     sysprof_capture_condition_unref                  (SysprofCaptureCondition       *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_ref                    (SysprofCaptureCondition       *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_new_and                (SysprofCaptureCondition       *left,
                                                                           SysprofCaptureCondition       *right);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_new_where_type_in      (guint                          n_types,
                                                                           const SysprofCaptureFrameType *types);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_new_where_time_between (gint64                         begin_time,
                                                                           gint64                         end_time);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_new_where_pid_in       (guint                          n_pids,
                                                                           const gint32                  *pids);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureCondition *sysprof_capture_condition_new_where_counter_in   (guint                          n_counters,
                                                                           const guint                   *counters);
SYSPROF_AVAILABLE_IN_ALL
gboolean                 sysprof_capture_condition_match                  (const SysprofCaptureCondition *self,
                                                                           const SysprofCaptureFrame     *frame);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureCondition, sysprof_capture_condition_unref)

G_END_DECLS
