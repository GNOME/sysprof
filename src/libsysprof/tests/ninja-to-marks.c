/* ninja-to-marks.c
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

#include <sysprof.h>

#include "line-reader-private.h"

int
main (int argc,
      char *argv[])
{
  SysprofCaptureWriter *writer;
  GError *error = NULL;
  gint64 start_time;
  gint64 max_end_time;

  sysprof_clock_init ();

  if (!(writer = sysprof_capture_writer_new ("ninja.log.syscap", 0)))
    g_error ("Failed to open syscap");

  start_time = max_end_time = g_get_monotonic_time ();

  for (int i = 1; i < argc; i++)
    {
      g_autofree char *contents = NULL;
      LineReader reader;
      char *line;
      gsize len;
      gsize line_len;

      if (!g_file_get_contents (argv[i], &contents, &len, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }

      line_reader_init (&reader, contents, len);
      while ((line = line_reader_next (&reader, &line_len)))
        {
          g_auto(GStrv) parts = NULL;
          gint64 begin_time;
          gint64 end_time;

          line[line_len] = 0;

          if (line[0] == '#')
            continue;

          parts = g_strsplit (line, "\t", 5);
          if (g_strv_length (parts) != 5)
            continue;

          begin_time = g_ascii_strtoll (parts[0], NULL, 10) * (SYSPROF_NSEC_PER_SEC/1000);
          end_time = g_ascii_strtoll (parts[1], NULL, 10) * (SYSPROF_NSEC_PER_SEC/1000);

          if (end_time > max_end_time)
            max_end_time = end_time;

          sysprof_capture_writer_add_mark (writer,
                                           start_time + begin_time,
                                           -1,
                                           -1,
                                           end_time - begin_time,
                                           "ninja", parts[4], parts[3]);

        }
    }

  _sysprof_capture_writer_set_time_range (writer,
                                          start_time,
                                          start_time + max_end_time);

  sysprof_capture_writer_flush (writer);
  sysprof_capture_writer_unref (writer);

  return 0;
}
