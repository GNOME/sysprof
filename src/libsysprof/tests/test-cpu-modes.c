/* test-cpu-modes.c
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

#include "config.h"

#include "../sysprof-cpu-modes-private.h"

static void
test_percentages (void)
{
  const SysprofCpuTimes previous = { .user = 100, .nice = 20, .system = 50,
                                     .idle = 200, .iowait = 10, .irq = 5,
                                     .softirq = 5, .steal = 3, .guest = 10,
                                     .guest_nice = 2 };
  SysprofCpuTimes current = { .user = 140, .nice = 30, .system = 70,
                              .idle = 220, .iowait = 20, .irq = 8,
                              .softirq = 7, .steal = 8, .guest = 20,
                              .guest_nice = 4 };
  double modes[SYSPROF_CPU_MODE_N];
  double usage;

  g_assert_true (_sysprof_cpu_times_percentages (&previous, &current, modes, &usage));
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_USER], 38. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_SYSTEM], 20. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_INTERRUPTS], 5. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_GUEST], 12. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_IOWAIT], 10. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (modes[SYSPROF_CPU_MODE_STEAL], 5. / 110. * 100., .00001);
  g_assert_cmpfloat_with_epsilon (usage, 75. / 110. * 100., .00001);

  {
    SysprofCpuModeTotals totals = { 0 };
    double other[SYSPROF_CPU_MODE_N] = { 10., 20., 5., 0., 5., 0. };

    _sysprof_cpu_mode_totals_add (&totals, modes, usage);
    _sysprof_cpu_mode_totals_add (&totals, other, 35.);
    g_assert_cmpuint (totals.count, ==, 2);
    g_assert_cmpfloat_with_epsilon (totals.usage / totals.count,
                                    (75. / 110. * 100. + 35.) / 2., .00001);
    g_assert_cmpfloat_with_epsilon (totals.modes[SYSPROF_CPU_MODE_SYSTEM] / totals.count,
                                    (20. / 110. * 100. + 20.) / 2., .00001);
  }

  g_assert_false (_sysprof_cpu_times_percentages (&current, &current, modes, &usage));
  current.guest = current.user + current.nice + previous.guest;
  g_assert_false (_sysprof_cpu_times_percentages (&previous, &current, modes, &usage));
  current.guest = previous.guest;
  current.system = previous.system - 1;
  g_assert_false (_sysprof_cpu_times_percentages (&previous, &current, modes, &usage));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/cpu-modes/percentages", test_percentages);
  return g_test_run ();
}
