/* sysprof-memory-sampler-private.h
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

#include "sysprof-perf-event-stream-private.h"
#include <sysprof-capture.h>

#define SYSPROF_MEMORY_SAMPLE_TYPE (PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP | \
                                    PERF_SAMPLE_TID | PERF_SAMPLE_TIME | \
                                    PERF_SAMPLE_ADDR | PERF_SAMPLE_PERIOD | \
                                    PERF_SAMPLE_READ | PERF_SAMPLE_CALLCHAIN | \
                                    PERF_SAMPLE_WEIGHT | PERF_SAMPLE_DATA_SRC)
#define SYSPROF_MEMORY_READ_FORMAT (PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING)

gboolean _sysprof_memory_pmu_configure (const char                  *path,
                                        guint                        latency,
                                        struct perf_event_attr      *attr,
                                        GArray                     **cpus,
                                        GError                     **error);
gboolean _sysprof_memory_sample_decode (const void                  *record,
                                        gsize                        size,
                                        guint64                      sample_type,
                                        guint64                      read_format,
                                        SysprofCaptureMemorySample  *sample,
                                        SysprofCaptureAddress       *stack);
