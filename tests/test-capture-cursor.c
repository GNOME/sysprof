#include <glib/gstdio.h>
#include <sysprof.h>

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

  writer = sp_capture_writer_new ("capture-file", 0);
  g_assert (writer != NULL);

  reader = sp_capture_reader_new ("capture-file", &error);
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
  g_clear_object (&cursor);

  sp_capture_reader_unref (reader);
  sp_capture_writer_unref (writer);

  g_unlink ("capture-file");
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/SpCaptureCursor/basic", test_cursor_basic);
  return g_test_run ();
}
