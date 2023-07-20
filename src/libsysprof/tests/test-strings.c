/* test-strings.c
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

#include <sysprof.h>

#include "sysprof-strings-private.h"

static void
test_basic (void)
{
  SysprofStrings *strings = sysprof_strings_new ();

  g_ref_string_release (sysprof_strings_get (strings, "123"));
  g_ref_string_release (sysprof_strings_get (strings, "456"));

  g_ref_string_release (sysprof_strings_get (strings, "123"));
  g_ref_string_release (sysprof_strings_get (strings, "456"));

  sysprof_strings_unref (strings);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/libsysprof/Strings/basic", test_basic);
  return g_test_run ();
}
