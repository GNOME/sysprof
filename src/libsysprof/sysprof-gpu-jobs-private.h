/* sysprof-gpu-jobs-private.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include <glib.h>

#include "sysprof-perf-event-stream-private.h"

typedef struct
{
  guint offset;
  guint size;
} SysprofGpuField;

typedef struct
{
  guint           id;
  SysprofGpuField context;
  SysprofGpuField seqno;
  SysprofGpuField dev;
  SysprofGpuField ring;
} SysprofGpuFormat;

typedef struct
{
  gint64 times[3];
  guint  emitted;
} SysprofGpuJobStages;

guint    _sysprof_gpu_job_add_stage       (SysprofGpuJobStages     *stages,
                                           guint                    stage,
                                           gint64                   time);
gboolean _sysprof_gpu_job_span_in_capture (gint64                   begin,
                                           gint64                   end,
                                           gint64                   started_at,
                                           gint64                   ended_at);
gboolean _sysprof_gpu_jobs_parse_format   (const char              *text,
                                           SysprofGpuFormat        *format,
                                           gboolean                 require_names);
gboolean _sysprof_gpu_jobs_read_sample    (const SysprofGpuFormat  *format,
                                           const guint8            *raw,
                                           gsize                    length,
                                           guint64                 *context,
                                           guint64                 *seqno,
                                           char                   **dev,
                                           char                   **ring);
gboolean _sysprof_gpu_jobs_sample_payload (const SysprofPerfEvent  *event,
                                           const guint8           **raw,
                                           gsize                   *length);
