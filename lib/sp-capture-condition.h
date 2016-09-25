/* sp-capture-condition.h
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

#ifndef SP_CAPTURE_CONDITION_H
#define SP_CAPTURE_CONDITION_H

#include "sp-capture-types.h"

G_BEGIN_DECLS

#define SP_TYPE_CAPTURE_CONDITION (sp_capture_condition_get_type())

GType               sp_capture_condition_get_type               (void);
SpCaptureCondition *sp_capture_condition_new_and                (SpCaptureCondition       *left,
                                                                 SpCaptureCondition       *right);
SpCaptureCondition *sp_capture_condition_new_where_type_in      (guint                     n_types,
                                                                 const SpCaptureFrameType *types);
SpCaptureCondition *sp_capture_condition_new_where_time_between (gint64                    begin_time,
                                                                 gint64                    end_time);
SpCaptureCondition *sp_capture_condition_new_where_pid_in       (guint                     n_pids,
                                                                 const GPid               *pids);
SpCaptureCondition *sp_capture_condition_new_where_counter_in   (guint                     n_counters,
                                                                 const guint              *counters);
gboolean            sp_capture_condition_match                  (const SpCaptureCondition *self,
                                                                 const SpCaptureFrame     *frame);

G_END_DECLS

#endif /* SP_CAPTURE_CONDITION_H */
