/* sp-capture-condition.h
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

G_BEGIN_DECLS

SpCaptureCondition *sp_capture_condition_copy                   (const SpCaptureCondition *self);
void                sp_capture_condition_unref                  (SpCaptureCondition       *self);
SpCaptureCondition *sp_capture_condition_ref                    (SpCaptureCondition       *self);
SpCaptureCondition *sp_capture_condition_new_and                (SpCaptureCondition       *left,
                                                                 SpCaptureCondition       *right);
SpCaptureCondition *sp_capture_condition_new_where_type_in      (guint                     n_types,
                                                                 const SpCaptureFrameType *types);
SpCaptureCondition *sp_capture_condition_new_where_time_between (gint64                    begin_time,
                                                                 gint64                    end_time);
SpCaptureCondition *sp_capture_condition_new_where_pid_in       (guint                     n_pids,
                                                                 const gint32             *pids);
SpCaptureCondition *sp_capture_condition_new_where_counter_in   (guint                     n_counters,
                                                                 const guint              *counters);
gboolean            sp_capture_condition_match                  (const SpCaptureCondition *self,
                                                                 const SpCaptureFrame     *frame);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpCaptureCondition, sp_capture_condition_unref)

G_END_DECLS
