/* sysprof-maps-parser.c
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

static GRegex *address_range_regex;

void
sysprof_maps_parser_init (SysprofMapsParser *self,
                          const char        *str,
                          gssize             len)
{
  line_reader_init (&self->reader, (char *)str, len);

  if (address_range_regex == NULL)
    address_range_regex = g_regex_new ("^([0-9a-f]+)-([0-9a-f]+) [r\\-][w\\-][x\\-][ps\\-] ([0-9a-f]+) [0-9a-f]+:[0-9a-f]+ ([0-9]+) +(.*)$",
                                       G_REGEX_OPTIMIZE,
                                       G_REGEX_MATCH_DEFAULT,
                                       NULL);
}

gboolean
sysprof_maps_parser_next (SysprofMapsParser  *self,
                          guint64            *out_begin_addr,
                          guint64            *out_end_addr,
                          guint64            *out_offset,
                          guint64            *out_inode,
                          char              **out_filename)
{
  const char *line;
  gsize len;

  while ((line = line_reader_next (&self->reader, &len)))
    {
      g_autoptr(GMatchInfo) match_info = NULL;

      if (g_regex_match_full (address_range_regex, line, len, 0, 0, &match_info, NULL))
        {
          g_autofree char *file = NULL;
          guint64 begin_addr;
          guint64 end_addr;
          guint64 inode;
          guint64 offset;
          gboolean is_vdso;
          int begin_addr_begin;
          int begin_addr_end;
          int end_addr_begin;
          int end_addr_end;
          int offset_begin;
          int offset_end;
          int inode_begin;
          int inode_end;
          int path_begin;
          int path_end;


          if (!g_match_info_fetch_pos (match_info, 1, &begin_addr_begin, &begin_addr_end) ||
              !g_match_info_fetch_pos (match_info, 2, &end_addr_begin, &end_addr_end) ||
              !g_match_info_fetch_pos (match_info, 3, &offset_begin, &offset_end) ||
              !g_match_info_fetch_pos (match_info, 4, &inode_begin, &inode_end) ||
              !g_match_info_fetch_pos (match_info, 5, &path_begin, &path_end))
            continue;

          begin_addr = g_ascii_strtoull (&line[begin_addr_begin], NULL, 16);
          end_addr = g_ascii_strtoull (&line[end_addr_begin], NULL, 16);
          offset = g_ascii_strtoull (&line[offset_begin], NULL, 16);
          inode = g_ascii_strtoull (&line[inode_begin], NULL, 10);

          if (memcmp (" (deleted",
                      &line[path_end] - strlen (" (deleted"),
                      strlen (" (deleted")) == 0)
            path_end -= strlen (" (deleted)");

          file = g_strndup (&line[path_begin], path_end-path_begin);

          is_vdso = strcmp ("[vdso]", file) == 0;

          if (is_vdso)
            inode = 0;

          if (is_vdso)
            offset = 0;

          *out_begin_addr = begin_addr;
          *out_end_addr = end_addr;
          *out_offset = offset;
          *out_inode = inode;
          *out_filename = g_steal_pointer (&file);

          return TRUE;
        }
    }

  return FALSE;
}
