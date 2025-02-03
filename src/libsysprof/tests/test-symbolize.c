#include "config.h"

#include <sysprof.h>

#include "sysprof-document-private.h"

static GMainLoop *main_loop;
static gboolean silent;
static gboolean no_bundled;
static gboolean debuginfod;
static char **debug_dirs;
static char *kallsyms_path;
static const GOptionEntry entries[] = {
  { "no-bundled", 'b', 0, G_OPTION_ARG_NONE, &no_bundled, "Ignore symbols bundled in capture file" },
  { "silent", 's', 0, G_OPTION_ARG_NONE, &silent, "Do not print symbol information" },
  { "debug-dir", 'd', 0, G_OPTION_ARG_FILENAME_ARRAY, &debug_dirs, "Specify external debug directory, may be repeated" },
  { "kallsyms", 'k', 0, G_OPTION_ARG_FILENAME, &kallsyms_path, "Specify path to kallsyms for kernel symbolizing" },
  { "debuginfod", 'D', 0, G_OPTION_ARG_NONE, &debuginfod, "Use debuginfod for automatically fetching debug symbols" },
  { 0 }
};

static void
load_cb (GObject      *object,
         GAsyncResult *result,
         gpointer      user_data)
{
  SysprofDocumentLoader *loader = (SysprofDocumentLoader *)object;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) traceables = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GString) str = NULL;
  SysprofSymbol *symbols[128];
  guint n_symbols;
  guint n_items;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!(document = sysprof_document_loader_load_finish (loader, result, &error)))
    g_error ("Failed to load document: %s", error->message);

  traceables = sysprof_document_list_traceables (document);
  n_items = g_list_model_get_n_items (traceables);

  if (silent)
    {
      g_main_loop_quit (main_loop);
      return;
    }

  str = g_string_new ("");

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (traceables, i);

      str->len = 0;
      str->str[0] = 0;

      g_assert (traceable != NULL);
      g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

      n_symbols = sysprof_document_symbolize_traceable (document,
                                                        traceable,
                                                        symbols,
                                                        G_N_ELEMENTS (symbols),
                                                        NULL);

      g_print ("%s depth=%u pid=%u\n",
               G_OBJECT_TYPE_NAME (traceable),
               n_symbols,
               sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable)));

      for (guint j = 0; j < n_symbols; j++)
        {
          SysprofSymbol *symbol = symbols[j];
          const char *name;
          const char *path;
          const char *nick;

          if (symbol != NULL)
            {
              name = sysprof_symbol_get_name (symbol);
              path = sysprof_symbol_get_binary_path (symbol);
              nick = sysprof_symbol_get_binary_nick (symbol);
            }
          else
            {
              name = path = nick = NULL;
            }

          g_string_append_printf (str,
                                  "  %02d: 0x%"G_GINT64_MODIFIER"x:",
                                  j,
                                  sysprof_document_traceable_get_stack_address (traceable, j));

          if (name)
            g_string_append_printf (str, " %s", name);

          if (path)
            g_string_append_printf (str, " [%s]", path);

          if (nick)
            g_string_append_printf (str, " (%s)", nick);

          g_string_append_c (str, '\n');
        }

      g_string_append (str, "  ================\n");

      write (STDOUT_FILENO, str->str, str->len);
    }

  g_print ("Document symbolized\n");

  g_main_loop_quit (main_loop);
}

int
main (int argc,
      char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;

  main_loop = g_main_loop_new (NULL, FALSE);
  context = g_option_context_new ("- test document symbolization");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (argc != 2 || !g_file_test (argv[1], G_FILE_TEST_EXISTS))
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  loader = sysprof_document_loader_new (argv[1]);

  if (TRUE)
    {
      g_autoptr(SysprofMultiSymbolizer) multi = sysprof_multi_symbolizer_new ();
      SysprofSymbolizer *elf = sysprof_elf_symbolizer_new ();
      g_autoptr(GFile) kallsyms_file = kallsyms_path ? g_file_new_for_path (kallsyms_path) : NULL;
      GFileInputStream *kallsyms_stream = kallsyms_file ? g_file_read (kallsyms_file, NULL, NULL) : NULL;

      if (debug_dirs)
        sysprof_elf_symbolizer_set_external_debug_dirs (SYSPROF_ELF_SYMBOLIZER (elf),
                                                        (const char * const *)debug_dirs);

      if (!no_bundled)
        sysprof_multi_symbolizer_take (multi, sysprof_bundled_symbolizer_new ());

      if (kallsyms_stream == NULL)
        sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
      else
        sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new_for_symbols (G_INPUT_STREAM (kallsyms_stream)));

      sysprof_multi_symbolizer_take (multi, elf);
      sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

#if HAVE_DEBUGINFOD
      if (debuginfod)
        {
          g_autoptr(SysprofSymbolizer) debuginfod_symbolizer = NULL;
          g_autoptr(GError) debuginfod_error = NULL;

          if (!(debuginfod_symbolizer = sysprof_debuginfod_symbolizer_new (&debuginfod_error)))
            g_warning ("Failed to create debuginfod symbolizer: %s", debuginfod_error->message);
          else
            sysprof_multi_symbolizer_take (multi, g_steal_pointer (&debuginfod_symbolizer));
        }
#endif

      sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));
    }

  sysprof_document_loader_load_async (loader, NULL, load_cb, NULL);
  g_main_loop_run (main_loop);

  return 0;
}
