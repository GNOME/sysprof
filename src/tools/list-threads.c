/* list-threads.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include <fcntl.h>
#include <stdlib.h>
#include <glib.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "../libsysprof/sysprof-capture-autocleanups.h"

static bool
foreach_cb (const SysprofCaptureFrame *frame,
            void                      *user_data)
{
  const SysprofCaptureSample *sample = (SysprofCaptureSample *)frame;
  GHashTable *seen = user_data;

  if (!g_hash_table_contains (seen, GINT_TO_POINTER (sample->tid)))
    g_hash_table_insert (seen,
                         GINT_TO_POINTER (sample->tid),
                         GINT_TO_POINTER (frame->pid));

  return true;
}

gint
main (gint   argc,
      gchar *argv[])
{
  static const SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_SAMPLE,
  };
  g_autoptr(GHashTable) seen = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;

  if (argc != 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return EXIT_FAILURE;
    }

  if (g_strcmp0 ("-", argv[1]) == 0)
    reader = sysprof_capture_reader_new_from_fd (dup (STDIN_FILENO));
  else
    reader = sysprof_capture_reader_new (argv[1]);

  if (reader == NULL)
    {
      g_printerr ("Failed to open %s capture\n", argv[1]);
      return EXIT_FAILURE;
    }

  seen = g_hash_table_new (NULL, NULL);

  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor,
      sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types));
  sysprof_capture_cursor_foreach (cursor, foreach_cb, seen);

  {
    GHashTableIter iter;
    gpointer k, v;

    g_hash_table_iter_init (&iter, seen);

    while (g_hash_table_iter_next (&iter, &k, &v))
      {
        g_print ("pid=%d  tid=%d\n",
                 GPOINTER_TO_INT (v),
                 GPOINTER_TO_INT (k));
      }
  }

  return EXIT_SUCCESS;
}
