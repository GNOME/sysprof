/* test-capture-cursor.c
 *
 * Copyright 2016o-2019 Christian Hergert <chergert@redhat.com>
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

#include <glib/gstdio.h>
#include <sysprof-capture.h>

static gboolean
increment (const SpCaptureFrame *frame,
           gpointer              user_data)
{
  gint *count= user_data;
  (*count)++;
  return TRUE;
}

static void
test_cursor_basic (void)
{
  SpCaptureReader *reader;
  SpCaptureWriter *writer;
  SpCaptureCursor *cursor;
  GError *error = NULL;
  gint64 t = SP_CAPTURE_CURRENT_TIME;
  guint i;
  gint r;
  gint count = 0;

  writer = sp_capture_writer_new ("capture-cursor-file", 0);
  g_assert (writer != NULL);

  sp_capture_writer_flush (writer);

  reader = sp_capture_reader_new ("capture-cursor-file", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  for (i = 0; i < 100; i++)
    {
      r = sp_capture_writer_add_timestamp (writer, t, i, -1);
      g_assert_cmpint (r, ==, TRUE);
    }

  sp_capture_writer_flush (writer);

  cursor = sp_capture_cursor_new (reader);
  sp_capture_cursor_foreach (cursor, increment, &count);
  g_assert_cmpint (count, ==, 100);
  g_clear_pointer (&cursor, sp_capture_cursor_unref);

  sp_capture_reader_unref (reader);
  sp_capture_writer_unref (writer);

  g_unlink ("capture-cursor-file");
}

int
main (int argc,
      char *argv[])
{
  sp_clock_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/SpCaptureCursor/basic", test_cursor_basic);
  return g_test_run ();
}
