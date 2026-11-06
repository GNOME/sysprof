/* test-memory-usage.c
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

#include "sysprof-memory-usage-private.h"

static void
test_missing_and_unitless (void)
{
  gint64 values[SYSPROF_MEMORY_USAGE_N_VALUES];
  gboolean present[SYSPROF_MEMORY_USAGE_N_VALUES];

  _sysprof_memory_usage_parse_meminfo ("MemTotal: 1000 kB\n"
                                       "MemAvailable: 600 kB\n"
                                       "MemFree: 100 kB\n"
                                       "AnonPages: 2\n"
                                       "Slab: invalid kB\n"
                                       "Dirty: 5 kB\n",
                                       values, present);
  g_assert_true (present[0]);
  g_assert_cmpint (values[0], ==, 400 * 1024);
  g_assert_cmpint (values[11], ==, 2);
  g_assert_true (present[11]);
  g_assert_false (present[12]);
  g_assert_cmpint (values[15], ==, 5 * 1024);
  g_assert_false (present[16]);
}

static void
test_malformed_and_missing_used (void)
{
  gint64 values[SYSPROF_MEMORY_USAGE_N_VALUES];
  gboolean present[SYSPROF_MEMORY_USAGE_N_VALUES];

  _sysprof_memory_usage_parse_meminfo ("MemTotal: 1000 kB\n"
                                       "MemAvailable: 2000 kB\n"
                                       "AnonPages: 9223372036854775808 kB\n"
                                       "Slab: 3 nonsense\n"
                                       "Writeback: 4 kB\n",
                                       values, present);
  g_assert_false (present[0]);
  g_assert_false (present[11]);
  g_assert_false (present[12]);
  g_assert_true (present[16]);
  g_assert_cmpint (values[16], ==, 4 * 1024);
}

static void
test_overflow_and_missing (void)
{
  gint64 values[SYSPROF_MEMORY_USAGE_N_VALUES];
  gboolean present[SYSPROF_MEMORY_USAGE_N_VALUES];

  _sysprof_memory_usage_parse_meminfo ("MemTotal: 1000 kB\n"
                                       "Dirty: 9223372036854775807 kB\n"
                                       "Slab: -1 kB\n"
                                       "Unknown: 4\n"
                                       "Writeback: 3 mB",
                                       values, present);
  g_assert_false (present[0]);
  g_assert_false (present[2]);
  g_assert_false (present[12]);
  g_assert_false (present[15]);
  g_assert_true (present[16]);
  g_assert_cmpint (values[16], ==, 3 * 1024 * 1024);

  _sysprof_memory_usage_parse_meminfo ("", values, present);
  for (guint i = 0; i < SYSPROF_MEMORY_USAGE_N_VALUES; i++)
    {
      g_assert_false (present[i]);
      g_assert_cmpint (values[i], ==, 0);
    }
}

static void
test_pressure_missing_and_malformed (void)
{
  gint64 values[2];
  gboolean present[2];

  _sysprof_memory_usage_parse_pressure ("some avg10=1.00\n"
                                        "full avg10=0.00 total=42\n",
                                        values, present);
  g_assert_false (present[0]);
  g_assert_true (present[1]);
  g_assert_cmpint (values[1], ==, 42);

  _sysprof_memory_usage_parse_pressure ("some avg10=1.00 total=123\n"
                                        "full avg10=0.00 total=bad\n",
                                        values, present);
  g_assert_true (present[0]);
  g_assert_cmpint (values[0], ==, 123);
  g_assert_false (present[1]);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/memory-usage/missing-and-unitless", test_missing_and_unitless);
  g_test_add_func ("/memory-usage/malformed-and-missing-used", test_malformed_and_missing_used);
  g_test_add_func ("/memory-usage/overflow-and-missing", test_overflow_and_missing);
  g_test_add_func ("/memory-usage/pressure-missing-and-malformed", test_pressure_missing_and_malformed);
  return g_test_run ();
}
