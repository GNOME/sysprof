/* sysprof-cpu-modes-private.h
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

typedef enum _SysprofCpuMode
{
  SYSPROF_CPU_MODE_USER,
  SYSPROF_CPU_MODE_SYSTEM,
  SYSPROF_CPU_MODE_INTERRUPTS,
  SYSPROF_CPU_MODE_GUEST,
  SYSPROF_CPU_MODE_IOWAIT,
  SYSPROF_CPU_MODE_STEAL,
  SYSPROF_CPU_MODE_N
} SysprofCpuMode;

typedef struct
{
  guint64 user;
  guint64 nice;
  guint64 system;
  guint64 idle;
  guint64 iowait;
  guint64 irq;
  guint64 softirq;
  guint64 steal;
  guint64 guest;
  guint64 guest_nice;
} SysprofCpuTimes;

typedef struct
{
  double usage;
  double modes[SYSPROF_CPU_MODE_N];
  guint  count;
} SysprofCpuModeTotals;

static inline void
_sysprof_cpu_mode_totals_add (SysprofCpuModeTotals *totals,
                              const double          modes[SYSPROF_CPU_MODE_N],
                              double                usage)
{
  g_assert (totals != NULL);
  g_assert (modes != NULL);

  totals->usage += usage;
  for (guint mode = 0; mode < SYSPROF_CPU_MODE_N; mode++)
    totals->modes[mode] += modes[mode];
  totals->count++;
}

static inline gboolean
_sysprof_cpu_times_percentages (const SysprofCpuTimes *previous,
                                const SysprofCpuTimes *current,
                                double                 modes[SYSPROF_CPU_MODE_N],
                                double                *usage)
{
  SysprofCpuTimes delta;
  guint64 total;
  guint64 guest;

  g_assert (previous != NULL);
  g_assert (current != NULL);
  g_assert (modes != NULL);
  g_assert (usage != NULL);

#define DELTA(field) G_STMT_START {               \
  if (current->field < previous->field)           \
    return FALSE;                                 \
  delta.field = current->field - previous->field; \
} G_STMT_END
  DELTA (user);
  DELTA (nice);
  DELTA (system);
  DELTA (idle);
  DELTA (iowait);
  DELTA (irq);
  DELTA (softirq);
  DELTA (steal);
  DELTA (guest);
  DELTA (guest_nice);
#undef DELTA

  total = delta.user + delta.nice + delta.system + delta.idle + delta.iowait +
          delta.irq + delta.softirq + delta.steal;
  guest = delta.guest + delta.guest_nice;
  if (total == 0 || guest > delta.user + delta.nice)
    return FALSE;

  modes[SYSPROF_CPU_MODE_USER] = 100. * (delta.user + delta.nice - guest) / total;
  modes[SYSPROF_CPU_MODE_SYSTEM] = 100. * delta.system / total;
  modes[SYSPROF_CPU_MODE_INTERRUPTS] = 100. * (delta.irq + delta.softirq) / total;
  modes[SYSPROF_CPU_MODE_GUEST] = 100. * guest / total;
  modes[SYSPROF_CPU_MODE_IOWAIT] = 100. * delta.iowait / total;
  modes[SYSPROF_CPU_MODE_STEAL] = 100. * delta.steal / total;
  *usage = modes[SYSPROF_CPU_MODE_USER] + modes[SYSPROF_CPU_MODE_SYSTEM] +
           modes[SYSPROF_CPU_MODE_INTERRUPTS] + modes[SYSPROF_CPU_MODE_GUEST];
  return TRUE;
}
