/* test-utils.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#include <sysprof-symbol-resolver-private.h>

static void
test_debug_dirs (void)
{
  g_auto(GStrv) test1 = NULL;
  g_auto(GStrv) test2 = NULL;

  test1 = _sysprof_symbol_resolver_guess_debug_dirs ("/usr/bin/foo");
  g_assert_nonnull (test1);
  g_assert_cmpstr (test1[0], ==, "/usr/lib/debug/bin");
  g_assert_cmpstr (test1[1], ==, "/usr/lib64/debug/bin");
  g_assert_cmpstr (test1[2], ==, NULL);

  test2 = _sysprof_symbol_resolver_guess_debug_dirs ("/usr/lib64/libc.so.6");
  g_assert_nonnull (test2);
  g_assert_cmpstr (test2[0], ==, "/usr/lib/debug/lib64");
  g_assert_cmpstr (test2[1], ==, "/usr/lib64/debug/lib64");
  g_assert_cmpstr (test2[2], ==, NULL);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Sysprof/SymbolResolver/guess_debug_dirs", test_debug_dirs);
  return g_test_run ();
}
