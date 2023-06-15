/* sysprof-time-span.c
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

#include "config.h"

#include "sysprof-time-span.h"

G_DEFINE_BOXED_TYPE (SysprofTimeSpan, sysprof_time_span, sysprof_time_span_copy, sysprof_time_span_free)

SysprofTimeSpan *
sysprof_time_span_copy (const SysprofTimeSpan *self)
{
  if (self == NULL)
    return NULL;

  return g_memdup2 (self, sizeof *self);
}

void
sysprof_time_span_free (SysprofTimeSpan *self)
{
  g_free (self);
}
