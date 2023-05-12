#include "config.h"

#include <sysprof-analyze.h>

#include "sysprof-document-private.h"

static GMainLoop *main_loop;

static void
symbolize_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GListModel) traceables = NULL;
  g_autoptr(GError) error = NULL;
  SysprofAddress addresses[128];
  guint n_items;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!(symbols = _sysprof_document_symbolize_finish (document, result, &error)))
    g_error ("Failed to symbolize: %s", error->message);

  traceables = sysprof_document_list_traceables (document);
  n_items = g_list_model_get_n_items (traceables);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (traceables, i);
      SysprofAddressContext last_context;
      guint depth;
      int pid;

      g_assert (traceable != NULL);
      g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

      last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
      pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));
      depth = sysprof_document_traceable_get_stack_addresses (traceable, addresses, G_N_ELEMENTS (addresses));

      g_print ("%s depth=%u pid=%u\n",
               G_OBJECT_TYPE_NAME (traceable), depth, pid);

      for (guint j = 0; j < depth; j++)
        {
          SysprofAddress address = addresses[j];
          SysprofSymbol *symbol = sysprof_document_symbols_lookup (symbols, pid, last_context, address);
          SysprofAddressContext context;

          if (sysprof_address_is_context_switch (address, &context))
            last_context = context;

          if (symbol != NULL)
            g_print ("  %02d: %p: %s [%s]\n",
                     j,
                     GSIZE_TO_POINTER (address),
                     sysprof_symbol_get_name (symbol),
                     sysprof_symbol_get_binary_path (symbol) ?: "<unresolved>");
          else
            g_print ("  %02d: %p:\n", j, GSIZE_TO_POINTER (address));
        }

      g_print ("  ================\n");
    }

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

  if (!(document = _sysprof_document_new (filename, &error)))
    {
      g_printerr ("Failed to load document: %s\n", error->message);
      return 1;
    }

  multi = sysprof_multi_symbolizer_new ();
  sysprof_multi_symbolizer_add (multi, sysprof_bundled_symbolizer_new ());

  _sysprof_document_symbolize_async (document,
                                     SYSPROF_SYMBOLIZER (multi),
                                     NULL,
                                     symbolize_cb,
                                     NULL);

  g_main_loop_run (main_loop);

  return 0;
}
