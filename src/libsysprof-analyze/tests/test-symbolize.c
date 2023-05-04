#include "config.h"

#include <sysprof-analyze.h>

static GMainLoop *main_loop;

static void
symbolize_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!(symbols = sysprof_document_symbolize_finish (document, result, &error)))
    g_error ("Failed to symbolize: %s", error->message);

  g_print ("Document symbolized\n");

  g_main_loop_quit (main_loop);
}

int
main (int argc,
      char *argv[])
{
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GError) error = NULL;
  const char *filename;

  main_loop = g_main_loop_new (NULL, FALSE);

  if (argc != 2 || !g_file_test (argv[1], G_FILE_TEST_EXISTS))
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  if (!(document = sysprof_document_new (filename, &error)))
    {
      g_printerr ("Failed to load document: %s\n", error->message);
      return 1;
    }

  multi = sysprof_multi_symbolizer_new ();
  sysprof_multi_symbolizer_add (multi, sysprof_bundled_symbolizer_new ());

  sysprof_document_symbolize_async (document,
                                    SYSPROF_SYMBOLIZER (multi),
                                    NULL,
                                    symbolize_cb,
                                    NULL);

  g_main_loop_run (main_loop);

  return 0;
}
