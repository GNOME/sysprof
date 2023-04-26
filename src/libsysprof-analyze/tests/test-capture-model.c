#include <errno.h>
#include <fcntl.h>

#include <sysprof-analyze.h>
#include <sysprof-capture.h>

int
main (int argc,
      char *argv[])
{
  SysprofDocument *document;
  const char *filename;
  GError *error = NULL;
  guint n_items;

  sysprof_clock_init ();

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  if (!(document = sysprof_document_new (filename, &error)))
    {
      g_printerr ("Failed to load %s: %s\n",
                  filename, error->message);
      return 1;
    }

  n_items = g_list_model_get_n_items (G_LIST_MODEL (document));

  for (guint i = 0; i < n_items; i++)
    {
      SysprofDocumentFrame *frame = g_list_model_get_item (G_LIST_MODEL (document), i);

      g_print ("%"G_GINT64_FORMAT" [pid %d] [cpu %d] (type %s)\n",
               sysprof_document_frame_get_time (frame),
               sysprof_document_frame_get_pid (frame),
               sysprof_document_frame_get_cpu (frame),
               G_OBJECT_TYPE_NAME (frame));

      g_clear_object (&frame);
    }

  g_printerr ("%u frames\n", n_items);

  g_clear_object (&document);

  return 0;
}
