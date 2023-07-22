/* test-maps-parser.c
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

#include "sysprof-maps-parser-private.h"

#define ADDR_FORMAT "0x%"G_GINT64_MODIFIER"x"

int
main (int argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;
  g_autofree char *contents = NULL;
  SysprofMapsParser parser;
  guint64 begin, end, offset, inode;
  char *file;
  gsize len;

  g_assert_cmpint (argc, >, 1);
  g_file_get_contents (argv[1], &contents, &len, &error);
  g_assert_no_error (error);

  sysprof_maps_parser_init (&parser, contents, len);
  while (sysprof_maps_parser_next (&parser, &begin, &end, &offset, &inode, &file))
    {
      g_print (ADDR_FORMAT" - "ADDR_FORMAT" "ADDR_FORMAT" %"G_GUINT64_FORMAT" %s\n",
               begin, end, offset, inode, file);
      g_free (file);
    }

  return 0;
}
