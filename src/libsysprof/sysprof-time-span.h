/* sysprof-time-span.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <glib-object.h>

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_TIME_SPAN (sysprof_time_span_get_type())

typedef struct _SysprofTimeSpan
{
  gint64 begin_nsec;
  gint64 end_nsec;
} SysprofTimeSpan;

SYSPROF_AVAILABLE_IN_ALL
GType            sysprof_time_span_get_type (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofTimeSpan *sysprof_time_span_copy     (const SysprofTimeSpan *self);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_time_span_free     (SysprofTimeSpan       *self);

#ifndef __GI_SCANNER__
static inline gint64
sysprof_time_span_duration (SysprofTimeSpan time_span)
{
  return time_span.end_nsec - time_span.begin_nsec;
}

static inline SysprofTimeSpan
sysprof_time_span_relative_to (SysprofTimeSpan time_span,
                               gint64          point)
{
  return (SysprofTimeSpan) {
    time_span.begin_nsec - point,
    time_span.end_nsec - point
  };
}

static inline void
sysprof_time_span_normalize (SysprofTimeSpan        time_span,
                             SysprofTimeSpan        allowed,
                             float                  values[SYSPROF_RESTRICT 2])
{
  double duration = allowed.end_nsec - allowed.begin_nsec;

  time_span = sysprof_time_span_relative_to (time_span, allowed.begin_nsec);

  values[0] = time_span.begin_nsec / duration;
  values[1] = time_span.end_nsec / duration;
}

static inline SysprofTimeSpan
sysprof_time_span_order (SysprofTimeSpan time_span)
{
  if (time_span.begin_nsec > time_span.end_nsec)
    return (SysprofTimeSpan) { time_span.end_nsec, time_span.begin_nsec };

  return time_span;
}

static inline gboolean
sysprof_time_span_clamp (SysprofTimeSpan *time_span,
                         SysprofTimeSpan  allowed)
{
  if (time_span->end_nsec <= allowed.begin_nsec ||
      time_span->begin_nsec >= allowed.end_nsec)
    {
      time_span->begin_nsec = time_span->end_nsec = 0;
      return FALSE;
    }

  if (time_span->begin_nsec < allowed.begin_nsec)
    time_span->begin_nsec = allowed.begin_nsec;

  if (time_span->end_nsec > allowed.end_nsec)
    time_span->end_nsec = allowed.end_nsec;

  return TRUE;
}

static inline char *
sysprof_time_offset_to_string (gint64 o)
{
  char str[32];

  if (o == 0)
    g_snprintf (str, sizeof str, "%.3lfs", .0);
  else if (o < 1000000)
    g_snprintf (str, sizeof str, "%.3lfμs", o/1000.);
  else if (o < (gint64)SYSPROF_NSEC_PER_SEC)
    g_snprintf (str, sizeof str, "%.3lfms", o/1000000.);
  else
    g_snprintf (str, sizeof str, "%.3lfs", o/(double)SYSPROF_NSEC_PER_SEC);

  return g_strdup (str);
}

static inline char *
sysprof_time_span_to_string (const SysprofTimeSpan *span)
{
  g_autofree char *begin = sysprof_time_offset_to_string (span->begin_nsec);
  g_autofree char *end = NULL;

  if (span->end_nsec == span->begin_nsec)
    return g_steal_pointer (&begin);

  end = sysprof_time_offset_to_string (span->end_nsec - span->begin_nsec);

  return g_strdup_printf ("%s (%s)", begin, end);
}

static inline gboolean
sysprof_time_span_equal (const SysprofTimeSpan *a,
                         const SysprofTimeSpan *b)
{
  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  return memcmp (a, b, sizeof *a) == 0;
}
#endif

G_END_DECLS
