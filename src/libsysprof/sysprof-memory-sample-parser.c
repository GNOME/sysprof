/* sysprof-memory-sample-parser.c
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

#include "config.h"

#include <string.h>

#include "sysprof-memory-sampler-private.h"

static gboolean
read_field (const guint8 **cursor,
            const guint8  *end,
            void          *value,
            gsize          size)
{
  g_assert (cursor != NULL);
  g_assert (*cursor <= end);

  if (size > end - *cursor)
    return FALSE;

  memcpy (value, *cursor, size);
  *cursor += size;

  return TRUE;
}

gboolean
_sysprof_memory_sample_decode (const void                 *record,
                               gsize                       size,
                               guint64                     sample_type,
                               guint64                     read_format,
                               SysprofCaptureMemorySample *sample,
                               SysprofCaptureAddress      *stack)
{
  const guint64 supported = (SYSPROF_MEMORY_SAMPLE_TYPE | PERF_SAMPLE_ID |
                             PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_WEIGHT_STRUCT);
  struct perf_event_header header;
  const guint8 *cursor;
  const guint8 *end;
  guint64 ignored;
  guint64 nr;
  guint32 pid_tid[2];

  g_return_val_if_fail (record != NULL, FALSE);
  g_return_val_if_fail (sample != NULL, FALSE);
  g_return_val_if_fail (stack != NULL, FALSE);

  cursor = record;
  end = cursor + size;
  memset (sample, 0, sizeof *sample);

  if (!read_field (&cursor, end, &header, sizeof header) ||
      header.type != PERF_RECORD_SAMPLE ||
      header.size != size ||
      (sample_type & ~supported) != 0 ||
      (sample_type & (PERF_SAMPLE_TID | PERF_SAMPLE_TIME)) != (PERF_SAMPLE_TID | PERF_SAMPLE_TIME) ||
      (sample_type & PERF_SAMPLE_WEIGHT_TYPE) == PERF_SAMPLE_WEIGHT_TYPE ||
      (read_format & ~SYSPROF_MEMORY_READ_FORMAT) != 0)
    return FALSE;

#define READ(value) \
  G_STMT_START { \
    if (!read_field (&cursor, end, &(value), sizeof (value))) \
      return FALSE; \
  } G_STMT_END

#define OPTIONAL(bit, field, flag) \
  G_STMT_START { \
    if (sample_type & (bit)) \
      { \
        READ (sample->field); \
        sample->flags |= (flag); \
      } \
  } G_STMT_END

  if (sample_type & PERF_SAMPLE_IDENTIFIER)
    READ (ignored);

  OPTIONAL (PERF_SAMPLE_IP, ip, SYSPROF_CAPTURE_MEMORY_SAMPLE_IP);

  READ (pid_tid);
  sample->frame.pid = pid_tid[0];
  sample->tid = pid_tid[1];

  READ (sample->frame.time);
  OPTIONAL (PERF_SAMPLE_ADDR, address, SYSPROF_CAPTURE_MEMORY_SAMPLE_ADDR);

  if (sample_type & PERF_SAMPLE_ID)
    READ (ignored);

  if (sample_type & PERF_SAMPLE_STREAM_ID)
    READ (ignored);

  if (sample_type & PERF_SAMPLE_CPU)
    READ (ignored);

  OPTIONAL (PERF_SAMPLE_PERIOD, period, SYSPROF_CAPTURE_MEMORY_SAMPLE_PERIOD);

  if (sample_type & PERF_SAMPLE_READ)
    {
      READ (ignored);

      if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        {
          READ (sample->time_enabled);
          sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_TIME_ENABLED;
        }

      if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        {
          READ (sample->time_running);
          sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_TIME_RUNNING;
        }
    }

  if (sample_type & PERF_SAMPLE_CALLCHAIN)
    {
      READ (nr);

      if (nr > (end - cursor) / sizeof (guint64))
        return FALSE;

      sample->n_addrs = MIN (nr, SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK);
      memcpy (stack, cursor, sample->n_addrs * sizeof *stack);
      cursor += nr * sizeof *stack;

      if (nr > sample->n_addrs)
        sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_STACK_TRUNCATED;
    }

  if (sample_type & PERF_SAMPLE_WEIGHT_TYPE)
    {
      union perf_sample_weight weight;

      READ (weight);

      if (sample_type & PERF_SAMPLE_WEIGHT_STRUCT)
        {
          sample->weight_type = SYSPROF_CAPTURE_MEMORY_WEIGHT_PERF_STRUCT;
          sample->weight = ((guint64)weight.var1_dw |
                            ((guint64)weight.var2_w << 32) |
                            ((guint64)weight.var3_w << 48));
        }
      else
        {
          sample->weight_type = SYSPROF_CAPTURE_MEMORY_WEIGHT_PERF;
          sample->weight = weight.full;
        }

      sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_WEIGHT;
    }

  OPTIONAL (PERF_SAMPLE_DATA_SRC, data_src, SYSPROF_CAPTURE_MEMORY_SAMPLE_DATA_SRC);

  if (header.misc & PERF_RECORD_MISC_EXACT_IP)
    sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_EXACT_IP;

  if ((header.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_KERNEL)
    sample->flags |= SYSPROF_CAPTURE_MEMORY_SAMPLE_KERNEL;

#undef READ
#undef OPTIONAL

  return cursor == end;
}
