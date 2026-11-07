/* sysprof-frame-cycle.h
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-document.h"
#include "sysprof-document-mark.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef struct _SysprofFrameCycle SysprofFrameCycle;

#define SYSPROF_TYPE_FRAME_CYCLE (sysprof_frame_cycle_get_type())

typedef enum
{
  SYSPROF_FRAME_CYCLE_COMPOSITOR,
  SYSPROF_FRAME_CYCLE_APPLICATION
} SysprofFrameCycleKind;

typedef enum
{
  SYSPROF_FRAME_CYCLE_INCOMPLETE,
  SYSPROF_FRAME_CYCLE_WORK_COMPLETE,
  SYSPROF_FRAME_CYCLE_READY,
  SYSPROF_FRAME_CYCLE_PRESENTED
} SysprofFrameCycleResult;

typedef struct
{
  gint64                   begin;
  gint64                   dispatch_end;
  gint64                   end;
  gint64                   presentation;
  gint64                   target_presentation;
  gint64                   submission_deadline;
  gint64                   kms_ready;
  gint64                   update_begin;
  gint64                   update_end;
  gint64                   layout_begin;
  gint64                   layout_end;
  gint64                   paint_begin;
  gint64                   paint_end;
  guint                    lane;
  guint                    row;
  guint                    clock_id;
  guint64                  gtk_clock;
  gint64                   frame_number;
  gint64                   gtk_frame_number;
  int                      matching_compositor;
  int                      pid;
  SysprofFrameCycleKind    kind;
  SysprofFrameCycleResult  result;
  gboolean                 correlation_estimated;
  gboolean                 presentation_estimated;
  SysprofDocumentMark     *dispatch_mark;
  SysprofDocumentMark     *presentation_mark;
} SysprofFrameCycleFrame;

SYSPROF_AVAILABLE_IN_52
const SysprofFrameCycleFrame *sysprof_frame_cycle_get_frame     (SysprofFrameCycle *self,
                                                                 guint              position);
SYSPROF_AVAILABLE_IN_52
const char                   *sysprof_frame_cycle_get_lane      (SysprofFrameCycle *self,
                                                                 guint              position);
SYSPROF_AVAILABLE_IN_52
guint                         sysprof_frame_cycle_get_lane_rows (SysprofFrameCycle *self,
                                                                 guint              position);
SYSPROF_AVAILABLE_IN_52
guint                         sysprof_frame_cycle_get_n_frames  (SysprofFrameCycle *self);
SYSPROF_AVAILABLE_IN_52
guint                         sysprof_frame_cycle_get_n_lanes   (SysprofFrameCycle *self);
SYSPROF_AVAILABLE_IN_52
GType                         sysprof_frame_cycle_get_type      (void);
SYSPROF_AVAILABLE_IN_52
SysprofFrameCycle            *sysprof_frame_cycle_new           (SysprofDocument   *document);
SYSPROF_AVAILABLE_IN_52
SysprofFrameCycle            *sysprof_frame_cycle_ref           (SysprofFrameCycle *self);
SYSPROF_AVAILABLE_IN_52
void                          sysprof_frame_cycle_unref         (SysprofFrameCycle *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofFrameCycle, sysprof_frame_cycle_unref)

G_END_DECLS
