/*
 * test-cpu-list.c
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

#include "config.h"

#include <errno.h>
#include <sched.h>

#include "sysprof-util-private.h"

static void
assert_cpu_list (const char  *text,
                 const guint *expected,
                 guint        n_expected)
{
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (text != NULL);
  g_assert (expected != NULL);

  cpus = _sysprof_parse_cpu_list (text, &error);
  g_assert_no_error (error);
  g_assert_nonnull (cpus);
  g_assert_cmpuint (cpus->len, ==, n_expected);
  g_assert_cmpmem (cpus->data, cpus->len * sizeof (guint),
                   expected, n_expected * sizeof (guint));
}

static void
test_cpu_list (void)
{
  const guint single[] = { 0 };
  const guint range[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
  const guint sparse[] = { 0, 2, 3, 4, 8, 10, 11 };
  const guint high[] = { 1024, 1025, 4096 };

  assert_cpu_list ("0", single, G_N_ELEMENTS (single));
  assert_cpu_list ("6-15\n", range, G_N_ELEMENTS (range));
  assert_cpu_list ("0,2-4,8,10-11\n", sparse, G_N_ELEMENTS (sparse));
  assert_cpu_list (" 1024-1025,4096\n", high, G_N_ELEMENTS (high));
}

static void
test_cpu_list_invalid (void)
{
  const char *invalid[] = {
    "", "\n", "-1", "+1", "cpu0", "0-", "4-2", "1,,2", "0,",
    "0,0", "0-4,2-6", "2,1", "0-3junk", "0\n1", "0-3, 5",
    "2147483648", "0-2147483648", "18446744073709551616",
  };

  for (guint i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
      g_autoptr(GArray) cpus = NULL;
      g_autoptr(GError) error = NULL;

      cpus = _sysprof_parse_cpu_list (invalid[i], &error);
      g_assert_null (cpus);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    }

  g_assert_null (_sysprof_parse_cpu_list ("", NULL));
}

static void
test_cpu_list_affinity (void)
{
  cpu_set_t allowed;
  int ret;

  CPU_ZERO (&allowed);
  ret = sched_getaffinity (0, sizeof allowed, &allowed);

  /* GLib's processor count also requires the affinity mask to fit in cpu_set_t. */
  if (ret == -1 && errno == EINVAL)
    {
      g_test_skip ("Kernel CPU affinity mask is larger than cpu_set_t");
      return;
    }

  g_assert_cmpint (ret, ==, 0);

  if (g_test_subprocess ())
    {
      g_autoptr(GArray) before = NULL;
      g_autoptr(GArray) after = NULL;
      g_autoptr(GError) error = NULL;
      cpu_set_t restricted;
      int cpu = -1;

      before = _sysprof_get_online_cpus (&error);
      g_assert_no_error (error);
      g_assert_nonnull (before);

      /* Use a high CPU ID so a count cannot be mistaken for an ID. */
      for (int i = CPU_SETSIZE - 1; i >= 0; i--)
        {
          if (CPU_ISSET (i, &allowed))
            {
              cpu = i;
              break;
            }
        }
      g_assert_cmpint (cpu, >=, 0);

      CPU_ZERO (&restricted);
      CPU_SET (cpu, &restricted);
      g_assert_cmpint (sched_setaffinity (0, sizeof restricted, &restricted), ==, 0);
      g_assert_cmpuint (g_get_num_processors (), ==, 1);

      after = _sysprof_get_online_cpus (&error);
      g_assert_no_error (error);
      g_assert_nonnull (after);
      g_assert_cmpmem (before->data, before->len * sizeof (guint),
                       after->data, after->len * sizeof (guint));
      return;
    }

  g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_passed ();
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Sysprof/CpuList/parse", test_cpu_list);
  g_test_add_func ("/Sysprof/CpuList/invalid", test_cpu_list_invalid);
  g_test_add_func ("/Sysprof/CpuList/affinity", test_cpu_list_affinity);
  return g_test_run ();
}
