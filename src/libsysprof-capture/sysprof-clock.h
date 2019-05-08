/* sysprof-clock.h
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

#include <glib.h>
#include <time.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

typedef gint SysprofClock;
typedef gint64 SysprofTimeStamp;
typedef gint32 SysprofTimeSysprofan;

SYSPROF_AVAILABLE_IN_ALL
SysprofClock sysprof_clock;

static inline SysprofTimeStamp
sysprof_clock_get_current_time (void)
{
  struct timespec ts;
  SysprofClock clock = sysprof_clock;

  if G_UNLIKELY (clock == -1)
    clock = CLOCK_MONOTONIC;
  clock_gettime (clock, &ts);

  return (ts.tv_sec * G_GINT64_CONSTANT (1000000000)) + ts.tv_nsec;
}

static inline SysprofTimeSysprofan
sysprof_clock_get_relative_time (SysprofTimeStamp epoch)
{
  return sysprof_clock_get_current_time () - epoch;
}

SYSPROF_AVAILABLE_IN_ALL
void sysprof_clock_init (void);

G_END_DECLS
