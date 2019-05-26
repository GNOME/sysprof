/* sysprof-cat.c
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-cat"

#include "config.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <sysprof-capture.h>
#include <unistd.h>

gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *tmpname = NULL;
  gsize len;
  gint fd;

  if (argc == 1)
    return 0;

  if (isatty (STDOUT_FILENO))
    {
      g_printerr ("stdout is a TTY, refusing to write binary data to stdout.\n");
      return EXIT_FAILURE;
    }

  for (guint i = 1; i < argc; i++)
    {
      if (!g_file_test (argv[i], G_FILE_TEST_IS_REGULAR))
        {
          g_printerr ("\"%s\" is not a regular file.\n", argv[i]);
          return EXIT_FAILURE;
        }
    }

  if (-1 == (fd = g_file_open_tmp (".sysprof-cat-XXXXXX", &tmpname, NULL)))
    {
      g_printerr ("Failed to create memfd for capture file.\n");
      return EXIT_FAILURE;
    }

  writer = sysprof_capture_writer_new_from_fd (fd, 4096*4);

  for (guint i = 1; i < argc; i++)
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;
      g_autoptr(GError) error = NULL;

      if (!(reader = sysprof_capture_reader_new (argv[i], &error)))
        {
          g_printerr ("Failed to create reader for \"%s\": %s\n",
                      argv[i], error->message);
          return EXIT_FAILURE;
        }

      if (!sysprof_capture_writer_cat (writer, reader, &error))
        {
          g_printerr ("Failed to join \"%s\": %s\n",
                      argv[i], error->message);
          return EXIT_FAILURE;
        }
    }

  if (g_file_get_contents (tmpname, &contents, &len, NULL))
    write (STDOUT_FILENO, contents, len);

  g_unlink (tmpname);

  return EXIT_SUCCESS;
}
