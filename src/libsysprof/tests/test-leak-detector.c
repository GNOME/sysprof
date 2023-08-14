/* test-leak-detector.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include <glib/gi18n.h>
#include <locale.h>

#include <sysprof.h>

#include "sysprof-document-private.h"
#include "sysprof-document-bitset-index-private.h"
#include "sysprof-leak-detector-private.h"

typedef struct _Augment
{
  guint64 toplevel : 1;
  guint64 size : 63;
  guint32 total;
} Augment;

static char *kallsyms_path;
static gboolean include_threads;
static GEnumClass *category_class;
static gboolean summary_only;
static const GOptionEntry entries[] = {
  { "kallsyms", 'k', 0, G_OPTION_ARG_FILENAME, &kallsyms_path, "The path to kallsyms to use for decoding", "PATH" },
  { "threads", 't', 0, G_OPTION_ARG_NONE, &include_threads, "Include threads in the stack traces" },
  { "summary", 's', 0, G_OPTION_ARG_NONE, &summary_only, "Only show summary" },
  { 0 }
};

static void
print_callgraph (GListModel *model,
                 guint       depth,
                 guint       total)
{
  guint n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofCallgraphFrame) frame = g_list_model_get_item (model, i);
      SysprofSymbol *symbol = sysprof_callgraph_frame_get_symbol (frame);
      Augment *aug = sysprof_callgraph_frame_get_augment (frame);
      SysprofCallgraphCategory category = sysprof_callgraph_frame_get_category (frame);
      const char*category_name = g_enum_get_value (category_class, category)->value_nick;
      char tstr[16];

      g_snprintf (tstr, sizeof tstr, "%.2lf%%", 100. * (aug->total / (double)total));

      g_print (" [%6u]  [%8s] ", aug->total, tstr);
      for (guint j = 0; j < depth; j++)
        g_print ("  ");
      g_print ("%s [%s]\n", sysprof_symbol_get_name (symbol), category_name);

      print_callgraph (G_LIST_MODEL (frame), depth+1, total);
    }
}

static void
summarize_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofCallgraphFrame *frame = SYSPROF_CALLGRAPH_FRAME (object);
  GMainLoop *main_loop = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GListModel) model = NULL;
  guint n_items;

  model = sysprof_callgraph_frame_summarize_finish (frame, result, &error);
  g_assert_no_error (error);
  g_assert_nonnull (model);
  g_assert_true (G_IS_LIST_MODEL (model));

  g_print ("\n\nSummary\n");

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofCategorySummary) summary = g_list_model_get_item (model, i);

      g_assert_nonnull (summary);
      g_assert_true (SYSPROF_IS_CATEGORY_SUMMARY (summary));

      g_print ("%s: %lf\n",
               g_enum_to_string (SYSPROF_TYPE_CALLGRAPH_CATEGORY,
                                 sysprof_category_summary_get_category (summary)),
               sysprof_category_summary_get_fraction (summary));
    }

  g_main_loop_quit (main_loop);
}

static void
callgraph_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = SYSPROF_DOCUMENT (object);
  GMainLoop *main_loop = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCallgraph) callgraph = sysprof_document_callgraph_finish (document, result, &error);
  g_autoptr(SysprofCallgraphFrame) root = NULL;
  Augment *aug;

  g_assert_no_error (error);
  g_assert_true (SYSPROF_IS_CALLGRAPH (callgraph));

  root = g_list_model_get_item (G_LIST_MODEL (callgraph), 0);
  aug = sysprof_callgraph_frame_get_augment (root);

  if (!summary_only)
    {
      g_print ("   Size   Percent\n");
      print_callgraph (G_LIST_MODEL (callgraph), 0, aug->total);
    }

  sysprof_callgraph_frame_summarize_async (root, NULL, summarize_cb, main_loop);
}

static void
augment_cb (SysprofCallgraph     *callgraph,
            SysprofCallgraphNode *node,
            SysprofDocumentFrame *frame,
            gboolean              summarize,
            gpointer              user_data)
{
  Augment *aug;
  gint64 size;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_ALLOCATION (frame));
  g_assert (user_data == NULL);

  size = sysprof_document_allocation_get_size (SYSPROF_DOCUMENT_ALLOCATION (frame));

  aug = sysprof_callgraph_get_augment (callgraph, node);
  aug->toplevel = TRUE;
  aug->size += size;

  for (; node ; node = sysprof_callgraph_node_parent (node))
    {
      aug = sysprof_callgraph_get_augment (callgraph, node);
      aug->total += size;
    }
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test callgraph generation");
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GListModel) records = NULL;
  g_autoptr(EggBitset) allocs = NULL;
  g_autoptr(EggBitset) leaks = NULL;
  g_autoptr(GTimer) timer = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;
  double elapsed;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    g_error ("%s", error->message);

  if (argc < 2)
    g_error ("usage: %s CAPTURE_FILE", argv[0]);

  category_class = g_type_class_ref (SYSPROF_TYPE_CALLGRAPH_CATEGORY);

  multi = sysprof_multi_symbolizer_new ();

  if (kallsyms_path)
    {
      g_autoptr(GFile) kallsyms_file = g_file_new_for_path (kallsyms_path);
      GFileInputStream *kallsyms_stream = g_file_read (kallsyms_file, NULL, NULL);

      sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new_for_symbols (G_INPUT_STREAM (kallsyms_stream)));
    }
  else
    {
      sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
    }

  sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  allocs = _sysprof_document_get_allocations (document);

  g_print ("Analyzing records for leaks...\n");
  timer = g_timer_new ();
  leaks = sysprof_leak_detector_detect (document, allocs);
  elapsed = g_timer_elapsed (timer, NULL);
  g_print ("Done in %lf seconds\n", elapsed);

  records = _sysprof_document_bitset_index_new (G_LIST_MODEL (document), leaks);

  if (include_threads)
    flags |= SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS;

  flags |= SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES;

  sysprof_document_callgraph_async (document,
                                    flags,
                                    records,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    NULL,
                                    callgraph_cb,
                                    main_loop);

  g_main_loop_run (main_loop);

  return 0;
}
