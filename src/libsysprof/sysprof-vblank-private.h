/* sysprof-vblank-private.h
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

#include <glib.h>

#include "sysprof-perf-event-stream-private.h"

typedef struct
{
  guint offset;
  guint size;
} SysprofVblankField;

typedef struct
{
  guint              id;
  SysprofVblankField crtc;
  SysprofVblankField sequence;
  SysprofVblankField time;
  SysprofVblankField high_prec;
} SysprofVblankFormat;

gboolean _sysprof_vblank_convert_time (gint64                     payload_time,
                                       gint64                     observation_time,
                                       gint64                     monotonic_offset,
                                       gint64                     raw_offset,
                                       gint64                    *capture_time);
gboolean _sysprof_vblank_parse_format (const char                *text,
                                       SysprofVblankFormat       *format);
gboolean _sysprof_vblank_read_lost    (const SysprofPerfEvent    *event,
                                       guint64                   *lost);
gboolean _sysprof_vblank_read_sample  (const SysprofVblankFormat *format,
                                       const guint8              *raw,
                                       gsize                      length,
                                       gint32                    *crtc,
                                       guint32                   *sequence,
                                       gint64                    *time,
                                       gboolean                  *high_prec);
