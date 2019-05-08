/* sysprof-clock.c
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

#define G_LOG_DOMAIN "sysprof-clock"

#include "config.h"

#include "sysprof-clock.h"

gint sysprof_clock = -1;

void
sysprof_clock_init (void)
{
  static const gint clock_ids[] = {
    CLOCK_MONOTONIC,
    CLOCK_MONOTONIC_RAW,
#ifdef __linux__
    CLOCK_MONOTONIC_COARSE,
    CLOCK_REALTIME_COARSE,
#endif
    CLOCK_REALTIME,
  };

  if (sysprof_clock != -1)
    return;

  for (guint i = 0; i < G_N_ELEMENTS (clock_ids); i++)
    {
      struct timespec ts;
      int clock_id = clock_ids [i];

      if (0 == clock_gettime (clock_id, &ts))
        {
          sysprof_clock = clock_id;
          return;
        }
    }

  g_assert_not_reached ();
}
