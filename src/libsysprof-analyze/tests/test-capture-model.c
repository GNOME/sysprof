#include <errno.h>
#include <fcntl.h>
#include <sysprof-analyze.h>

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

  g_print ("%u frames\n", n_items);

  for (guint i = 0; i < n_items; i++)
    {
      SysprofCaptureFrameObject *obj = g_list_model_get_item (G_LIST_MODEL (document), i);

      g_clear_object (&obj);
    }

  g_clear_object (&document);

  return 0;
}
