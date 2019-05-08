/* test-capture.c
 *
 * Copyright 2016 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "test-capture"

#include "config.h"

#include <glib/gstdio.h>
#include <sysprof-capture.h>

static void
test_reader_basic (void)
{
  SysprofCaptureReader *reader;
  SysprofCaptureWriter *writer;
  GError *error = NULL;
  gint64 t = SYSPROF_CAPTURE_CURRENT_TIME;
  guint i;
  gint r;

  writer = sysprof_capture_writer_new ("capture-file", 0);
  g_assert (writer != NULL);

  sysprof_capture_writer_flush (writer);

  reader = sysprof_capture_reader_new ("capture-file", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  for (i = 0; i < 100; i++)
    {
      gchar str[16];

      g_snprintf (str, sizeof str, "%d", i);

      r = sysprof_capture_writer_add_map (writer, t, -1, -1, i, i + 1, i + 2, i + 3, str);
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 100; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureMap *map;
      gchar str[16];

      g_snprintf (str, sizeof str, "%d", i);

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_MAP);

      map = sysprof_capture_reader_read_map (reader);

      g_assert (map != NULL);
      g_assert_cmpint (map->frame.pid, ==, -1);
      g_assert_cmpint (map->frame.cpu, ==, -1);
      g_assert_cmpint (map->start, ==, i);
      g_assert_cmpint (map->end, ==, i + 1);
      g_assert_cmpint (map->offset, ==, i + 2);
      g_assert_cmpint (map->inode, ==, i + 3);
      g_assert_cmpstr (map->filename, ==, str);
    }

  /* Now that we have read a frame, we should start having updated
   * end times with each incoming frame.
   */
  g_assert_cmpint (0, !=, sysprof_capture_reader_get_end_time (reader));

  for (i = 0; i < 100; i++)
    {
      r = sysprof_capture_writer_add_timestamp (writer, t, i, -1);
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 100; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureTimestamp *ts;

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_TIMESTAMP);

      ts = sysprof_capture_reader_read_timestamp (reader);

      g_assert (ts != NULL);
      g_assert_cmpint (ts->frame.cpu, ==, i);
      g_assert_cmpint (ts->frame.pid, ==, -1);
    }

  for (i = 0; i < 100; i++)
    {
      r = sysprof_capture_writer_add_exit (writer, t, i, -1);
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 100; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureExit *ex;

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_EXIT);

      ex = sysprof_capture_reader_read_exit (reader);

      g_assert (ex != NULL);
      g_assert_cmpint (ex->frame.cpu, ==, i);
      g_assert_cmpint (ex->frame.pid, ==, -1);
    }

  for (i = 0; i < 100; i++)
    {
      char cmdline[32];

      g_snprintf (cmdline, sizeof cmdline, "program-%d", i);
      r = sysprof_capture_writer_add_process (writer, t, -1, i, cmdline);
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 100; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureProcess *pr;
      char str[32];

      g_snprintf (str, sizeof str, "program-%d", i);

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_PROCESS);

      pr = sysprof_capture_reader_read_process (reader);

      g_assert (pr != NULL);
      g_assert_cmpint (pr->frame.cpu, ==, -1);
      g_assert_cmpint (pr->frame.pid, ==, i);
      g_assert_cmpstr (pr->cmdline, ==, str);
    }

  for (i = 0; i < 100; i++)
    {
      r = sysprof_capture_writer_add_fork (writer, t, i, -1, i);
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 100; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureFork *ex;

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_FORK);

      ex = sysprof_capture_reader_read_fork (reader);

      g_assert (ex != NULL);
      g_assert_cmpint (ex->frame.cpu, ==, i);
      g_assert_cmpint (ex->frame.pid, ==, -1);
      g_assert_cmpint (ex->child_pid, ==, i);
    }

  {
    SysprofCaptureCounter counters[10];
    guint base = sysprof_capture_writer_request_counter (writer, G_N_ELEMENTS (counters));

    t = SYSPROF_CAPTURE_CURRENT_TIME;

    for (i = 0; i < G_N_ELEMENTS (counters); i++)
      {
        g_snprintf (counters[i].category, sizeof counters[i].category, "cat%d", i);
        g_snprintf (counters[i].name, sizeof counters[i].name, "name%d", i);
        g_snprintf (counters[i].description, sizeof counters[i].description, "desc%d", i);
        counters[i].id = base + i;
        counters[i].type = 0;
        counters[i].value.v64 = i * G_GINT64_CONSTANT (100000000000);
      }

    r = sysprof_capture_writer_define_counters (writer, t, -1, -1, counters, G_N_ELEMENTS (counters));
    g_assert_cmpint (r, ==, TRUE);
  }

  sysprof_capture_writer_flush (writer);

  {
    const SysprofCaptureFrameCounterDefine *def;

    def = sysprof_capture_reader_read_counter_define (reader);
    g_assert (def != NULL);
    g_assert_cmpint (def->n_counters, ==, 10);

    for (i = 0; i < def->n_counters; i++)
      {
        g_autofree gchar *cat = g_strdup_printf ("cat%d", i);
        g_autofree gchar *name = g_strdup_printf ("name%d", i);
        g_autofree gchar *desc = g_strdup_printf ("desc%d", i);

        g_assert_cmpstr (def->counters[i].category, ==, cat);
        g_assert_cmpstr (def->counters[i].name, ==, name);
        g_assert_cmpstr (def->counters[i].description, ==, desc);
      }
  }

  for (i = 0; i < 1000; i++)
    {
      guint ids[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
      SysprofCaptureCounterValue values[10] = { {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10} };

      r = sysprof_capture_writer_set_counters (writer, t, -1,  -1, ids, values, G_N_ELEMENTS (values));
      g_assert_cmpint (r, ==, TRUE);
    }

  sysprof_capture_writer_flush (writer);

  for (i = 0; i < 1000; i++)
    {
      const SysprofCaptureFrameCounterSet *set;

      set = sysprof_capture_reader_read_counter_set (reader);
      g_assert (set != NULL);

      /* 8 per chunk */
      g_assert_cmpint (set->n_values, ==, 2);

      g_assert_cmpint (1, ==, set->values[0].ids[0]);
      g_assert_cmpint (2, ==, set->values[0].ids[1]);
      g_assert_cmpint (3, ==, set->values[0].ids[2]);
      g_assert_cmpint (4, ==, set->values[0].ids[3]);
      g_assert_cmpint (5, ==, set->values[0].ids[4]);
      g_assert_cmpint (6, ==, set->values[0].ids[5]);
      g_assert_cmpint (7, ==, set->values[0].ids[6]);
      g_assert_cmpint (8, ==, set->values[0].ids[7]);
      g_assert_cmpint (9, ==, set->values[1].ids[0]);
      g_assert_cmpint (10, ==, set->values[1].ids[1]);
      g_assert_cmpint (1, ==, set->values[0].values[0].v64);
      g_assert_cmpint (2, ==, set->values[0].values[1].v64);
      g_assert_cmpint (3, ==, set->values[0].values[2].v64);
      g_assert_cmpint (4, ==, set->values[0].values[3].v64);
      g_assert_cmpint (5, ==, set->values[0].values[4].v64);
      g_assert_cmpint (6, ==, set->values[0].values[5].v64);
      g_assert_cmpint (7, ==, set->values[0].values[6].v64);
      g_assert_cmpint (8, ==, set->values[0].values[7].v64);
      g_assert_cmpint (9, ==, set->values[1].values[0].v64);
      g_assert_cmpint (10, ==, set->values[1].values[1].v64);
    }

  for (i = 0; i < 1000; i++)
    {
      SysprofCaptureAddress addr;
      gchar str[32];

      g_snprintf (str, sizeof str, "jitstring-%d", i);

      addr = sysprof_capture_writer_add_jitmap (writer, str);
      g_assert_cmpint (addr, ==, (i + 1) | SYSPROF_CAPTURE_JITMAP_MARK);
    }

  sysprof_capture_writer_flush (writer);

  i = 0;

  for (;;)
    {
      SysprofCaptureFrameType type = -1;
      g_autoptr(GHashTable) ret = NULL;

      if (sysprof_capture_reader_peek_type (reader, &type))
        g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_JITMAP);
      else
        break;

      ret = sysprof_capture_reader_read_jitmap (reader);
      g_assert (ret != NULL);

      i += g_hash_table_size (ret);
    }

  g_assert_cmpint (1000, ==, i);

  {
    SysprofCaptureFrameType type = -1;

    if (sysprof_capture_reader_peek_type (reader, &type))
      g_assert_not_reached ();
  }

  for (i = 1; i <= 1000; i++)
    {
      SysprofCaptureAddress *addrs;
      guint j;

      addrs = alloca (i * sizeof *addrs);

      for (j = 0; j < i; j++)
        addrs[j] = i;

      if (!sysprof_capture_writer_add_sample (writer, t, -1, -1, -2, addrs, i))
        g_assert_not_reached ();
    }

  sysprof_capture_writer_flush (writer);

  for (i = 1; i <= 1000; i++)
    {
      SysprofCaptureFrameType type = -1;
      const SysprofCaptureSample *sample;
      guint j;

      if (!sysprof_capture_reader_peek_type (reader, &type))
        g_assert_not_reached ();

      g_assert_cmpint (type, ==, SYSPROF_CAPTURE_FRAME_SAMPLE);

      sample = sysprof_capture_reader_read_sample (reader);

      g_assert (sample != NULL);
      g_assert_cmpint (sample->frame.time, ==, t);
      g_assert_cmpint (sample->frame.cpu, ==, -1);
      g_assert_cmpint (sample->frame.pid, ==, -1);
      g_assert_cmpint (sample->tid, ==, -2);
      g_assert_cmpint (sample->n_addrs, ==, i);

      for (j = 0; j < i; j++)
        g_assert_cmpint (sample->addrs[j], ==, i);
    }

  {
    SysprofCaptureFrameType type = -1;

    if (sysprof_capture_reader_peek_type (reader, &type))
      g_assert_not_reached ();
  }

  r = sysprof_capture_writer_save_as (writer, "capture-file.bak", &error);
  g_assert_no_error (error);
  g_assert (r);
  g_assert (g_file_test ("capture-file.bak", G_FILE_TEST_IS_REGULAR));

  /* make sure contents are equal */
  {
    g_autofree gchar *buf1 = NULL;
    g_autofree gchar *buf2 = NULL;
    gsize buf1len = 0;
    gsize buf2len = 0;

    r = g_file_get_contents ("capture-file.bak", &buf1, &buf1len, &error);
    g_assert_no_error (error);
    g_assert_true (r);

    r = g_file_get_contents ("capture-file", &buf2, &buf2len, &error);
    g_assert_no_error (error);
    g_assert_true (r);

    g_assert_cmpint (buf1len, >, 0);
    g_assert_cmpint (buf2len, >, 0);

    g_assert_cmpint (buf1len, ==, buf2len);
    g_assert_true (0 == memcmp (buf1, buf2, buf1len));
  }

  g_clear_pointer (&writer, sysprof_capture_writer_unref);
  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  reader = sysprof_capture_reader_new ("capture-file.bak", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  for (i = 0; i < 2; i++)
    {
      SysprofCaptureFrameType type = -1;
      guint count = 0;

      while (sysprof_capture_reader_peek_type (reader, &type))
        {
          count++;
          if (!sysprof_capture_reader_skip (reader))
            g_assert_not_reached ();
        }

      g_assert_cmpint (count, >, 1500);

      sysprof_capture_reader_reset (reader);
    }

  sysprof_capture_reader_unref (reader);

  g_unlink ("capture-file");
  g_unlink ("capture-file.bak");
}

static void
test_writer_splice (void)
{
  SysprofCaptureWriter *writer1;
  SysprofCaptureWriter *writer2;
  SysprofCaptureReader *reader;
  SysprofCaptureFrameType type;
  GError *error = NULL;
  guint i;
  gint r;

  writer1 = sysprof_capture_writer_new ("writer1.syscap", 0);
  writer2 = sysprof_capture_writer_new ("writer2.syscap", 0);

  for (i = 0; i < 1000; i++)
    sysprof_capture_writer_add_timestamp (writer1, SYSPROF_CAPTURE_CURRENT_TIME, -1, -1);

  r = sysprof_capture_writer_splice (writer1, writer2, &error);
  g_assert_no_error (error);
  g_assert (r == TRUE);

  g_clear_pointer (&writer1, sysprof_capture_writer_unref);
  g_clear_pointer (&writer2, sysprof_capture_writer_unref);

  reader = sysprof_capture_reader_new ("writer2.syscap", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  for (i = 0; i < 1000; i++)
    {
      const SysprofCaptureTimestamp *ts = sysprof_capture_reader_read_timestamp (reader);

      g_assert (ts != NULL);
      g_assert_cmpint (ts->frame.cpu, ==, -1);
      g_assert_cmpint (ts->frame.pid, ==, -1);
      g_assert_cmpint (ts->frame.time, >, 0);
    }

  r = sysprof_capture_reader_peek_type (reader, &type);
  g_assert_cmpint (r, ==, FALSE);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  g_unlink ("writer1.syscap");
  g_unlink ("writer2.syscap");
}

static void
test_reader_splice (void)
{
  SysprofCaptureWriter *writer1;
  SysprofCaptureWriter *writer2;
  SysprofCaptureReader *reader;
  SysprofCaptureFrameType type;
  GError *error = NULL;
  guint i;
  guint count;
  gint r;

  writer1 = sysprof_capture_writer_new ("writer1.syscap", 0);
  writer2 = sysprof_capture_writer_new ("writer2.syscap", 0);

  for (i = 0; i < 1000; i++)
    sysprof_capture_writer_add_timestamp (writer1, SYSPROF_CAPTURE_CURRENT_TIME, -1, -1);

  r = sysprof_capture_writer_flush (writer1);
  g_assert_cmpint (r, ==, TRUE);

  g_clear_pointer (&writer1, sysprof_capture_writer_unref);

  reader = sysprof_capture_reader_new ("writer1.syscap", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  /* advance to the end of the reader to non-start boundary for fd */

  for (i = 0; i < 1000; i++)
    {
      const SysprofCaptureTimestamp *ts = sysprof_capture_reader_read_timestamp (reader);

      g_assert (ts != NULL);
      g_assert_cmpint (ts->frame.cpu, ==, -1);
      g_assert_cmpint (ts->frame.pid, ==, -1);
      g_assert_cmpint (ts->frame.time, >, 0);
    }

  r = sysprof_capture_reader_peek_type (reader, &type);
  g_assert_cmpint (r, ==, FALSE);

  r = sysprof_capture_reader_splice (reader, writer2, &error);
  g_assert_no_error (error);
  g_assert_cmpint (r, ==, TRUE);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);
  g_clear_pointer (&writer2, sysprof_capture_writer_unref);

  reader = sysprof_capture_reader_new ("writer2.syscap", 0);

  for (i = 0; i < 1000; i++)
    {
      const SysprofCaptureTimestamp *ts = sysprof_capture_reader_read_timestamp (reader);

      g_assert (ts != NULL);
      g_assert_cmpint (ts->frame.cpu, ==, -1);
      g_assert_cmpint (ts->frame.pid, ==, -1);
      g_assert_cmpint (ts->frame.time, >, 0);
    }

  r = sysprof_capture_reader_peek_type (reader, &type);
  g_assert_cmpint (r, ==, FALSE);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  reader = sysprof_capture_reader_new ("writer2.syscap", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  r = sysprof_capture_reader_save_as (reader, "writer3.syscap", &error);
  g_assert_no_error (error);
  g_assert_cmpint (r, ==, TRUE);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  reader = sysprof_capture_reader_new ("writer3.syscap", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  count = 0;
  while (sysprof_capture_reader_skip (reader))
    count++;
  g_assert_cmpint (count, ==, 1000);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  g_unlink ("writer1.syscap");
  g_unlink ("writer2.syscap");
  g_unlink ("writer3.syscap");
}

static void
test_reader_writer_mark (void)
{
  SysprofCaptureWriter *writer;
  SysprofCaptureReader *reader;
  const SysprofCaptureMark *mark;
  SysprofCaptureFrameType type;
  GError *error = NULL;
  gint r;

  writer = sysprof_capture_writer_new ("mark1.syscap", 0);

  sysprof_capture_writer_add_mark (writer, SYSPROF_CAPTURE_CURRENT_TIME, -1, -1, 125, "thread-0", "Draw", "hdmi-1");
  sysprof_capture_writer_add_mark (writer, SYSPROF_CAPTURE_CURRENT_TIME, -1, -1, 0, "thread-1", "Deadline", "hdmi-1");

  g_clear_pointer (&writer, sysprof_capture_writer_unref);

  reader = sysprof_capture_reader_new ("mark1.syscap", &error);
  g_assert_no_error (error);
  g_assert (reader != NULL);

  mark = sysprof_capture_reader_read_mark (reader);
  g_assert_nonnull (mark);
  g_assert_cmpstr (mark->group, ==, "thread-0");
  g_assert_cmpstr (mark->name, ==, "Draw");
  g_assert_cmpint (mark->duration, ==, 125);
  g_assert_cmpstr (mark->message, ==, "hdmi-1");
  g_assert_cmpint (mark->frame.time, >, 0);
  g_assert_cmpint (mark->frame.cpu, ==, -1);

  mark = sysprof_capture_reader_read_mark (reader);
  g_assert_nonnull (mark);
  g_assert_cmpstr (mark->group, ==, "thread-1");
  g_assert_cmpstr (mark->name, ==, "Deadline");
  g_assert_cmpint (mark->duration, ==, 0);
  g_assert_cmpstr (mark->message, ==, "hdmi-1");
  g_assert_cmpint (mark->frame.time, >, 0);
  g_assert_cmpint (mark->frame.cpu, ==, -1);

  r = sysprof_capture_reader_peek_type (reader, &type);
  g_assert_cmpint (r, ==, FALSE);

  g_clear_pointer (&reader, sysprof_capture_reader_unref);

  g_unlink ("mark1.syscap");
}

int
main (int argc,
      char *argv[])
{
  sysprof_clock_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/SysprofCapture/ReaderWriter", test_reader_basic);
  g_test_add_func ("/SysprofCapture/Writer/splice", test_writer_splice);
  g_test_add_func ("/SysprofCapture/Reader/splice", test_reader_splice);
  g_test_add_func ("/SysprofCapture/ReaderWriter/mark", test_reader_writer_mark);
  return g_test_run ();
}
