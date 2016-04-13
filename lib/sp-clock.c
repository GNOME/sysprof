/* sp-clock.c
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

#include "sp-clock.h"

gint sp_clock = -1;

void
sp_clock_init (void)
{
  static const gint clock_ids[] = {
    CLOCK_MONOTONIC_RAW,
    CLOCK_MONOTONIC_COARSE,
    CLOCK_MONOTONIC,
    CLOCK_REALTIME_COARSE,
    CLOCK_REALTIME,
  };
  guint i;

  if (sp_clock != -1)
    return;

  for (i = 0; i < G_N_ELEMENTS (clock_ids); i++)
    {
      struct timespec ts;
      int clock_id = clock_ids [i];

      if (0 == clock_gettime (clock_id, &ts))
        {
          sp_clock = clock_id;
          return;
        }
    }

  g_assert_not_reached ();
}
