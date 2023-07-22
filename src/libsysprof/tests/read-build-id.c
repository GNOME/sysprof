/* read-build-id.c
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

#include "config.h"

#include "elfparser.h"

int
main (int argc,
      char *argv[])
{
  for (guint i = 1; i < argc; i++)
    {
      g_autoptr(GError) error = NULL;
      const char *filename = argv[i];
      ElfParser *parser = elf_parser_new (filename, &error);
      const char *build_id;
      const char *debug_link;
      guint crc;

      if (parser == NULL)
        {
          if (error != NULL)
            g_printerr ("%s: %s\n", filename, error->message);
          else
            g_printerr ("%s: Failed to load as ELF\n", filename);
          g_clear_error (&error);
          continue;
        }

      build_id = elf_parser_get_build_id (parser);
      debug_link = elf_parser_get_debug_link (parser, &crc);

      g_print ("%s: %s (%s)\n", filename, build_id, debug_link);

      elf_parser_free (parser);
    }

  return 0;
}
