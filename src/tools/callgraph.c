/* callgraph.c
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

#include <gtk/gtk.h>
#include <sysprof-analyze.h>

static GMainLoop *main_loop;
static char *kallsyms_path;
static char *filename;
static double total;
static const GOptionEntry entries[] = {
  { "kallsyms", 'k', 0, G_OPTION_ARG_FILENAME, &kallsyms_path, "The path to kallsyms to use for decoding", "PATH" },
  { 0 }
};

typedef struct _Augment
{
  guint32 size;
  guint32 total;
} Augment;

static gboolean
on_key_pressed_cb (GtkEventControllerKey *key,
                   guint                  keyval,
                   guint                  keycode,
                   GdkModifierType        state,
                   GtkTreeExpander       *expander)
{
  GtkTreeListRow *row = gtk_tree_expander_get_list_row (expander);

  if (keyval == GDK_KEY_Right)
    {
      gtk_tree_list_row_set_expanded (row, TRUE);
      return TRUE;
    }

  if (keyval == GDK_KEY_Left)
    {
      gtk_tree_list_row_set_expanded (row, FALSE);
      return TRUE;
    }

  return FALSE;
}

static void
function_setup (GtkSignalListItemFactory *factory,
                GtkListItem              *list_item,
                SysprofCallgraph         *callgraph)
{
  GtkEventController *controller;
  GtkWidget *expander;
  GtkWidget *text;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));
  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  expander = gtk_tree_expander_new ();
  text = g_object_new (GTK_TYPE_INSCRIPTION,
                       "hexpand", TRUE,
                       NULL);
  gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), text);
  gtk_list_item_set_child (list_item, expander);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller,
                    "key-pressed",
                    G_CALLBACK (on_key_pressed_cb),
                    expander);
  gtk_widget_add_controller (GTK_WIDGET (expander), controller);
}

static void
function_bind (GtkSignalListItemFactory *factory,
               GtkListItem              *list_item,
               SysprofCallgraph         *callgraph)
{
  SysprofCallgraphFrame *frame;
  GtkTreeListRow *row;
  SysprofSymbol *symbol;
  const char *name;
  GtkWidget *expander;
  GtkWidget *text;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));
  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  row = gtk_list_item_get_item (list_item);
  frame = gtk_tree_list_row_get_item (row);
  symbol = sysprof_callgraph_frame_get_symbol (frame);
  name = sysprof_symbol_get_name (symbol);

  expander = gtk_list_item_get_child (list_item);
  text = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander));
  gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);
  gtk_inscription_set_text (GTK_INSCRIPTION (text), name);
}

static void
total_setup (GtkSignalListItemFactory *factory,
             GtkListItem              *list_item,
             SysprofCallgraph         *callgraph)
{
  GtkWidget *child;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));
  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  child = g_object_new (GTK_TYPE_PROGRESS_BAR,
                        "show-text", TRUE,
                        NULL);
  gtk_list_item_set_child (list_item, child);
}

static void
total_bind (GtkSignalListItemFactory *factory,
            GtkListItem              *list_item,
            gpointer                  user_data)
{
  g_autofree char *text = NULL;
  SysprofCallgraphFrame *frame;
  GtkTreeListRow *row;
  GtkWidget *child;
  double fraction;
  Augment *aug;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));

  row = gtk_list_item_get_item (list_item);
  frame = gtk_tree_list_row_get_item (row);
  aug = sysprof_callgraph_frame_get_augment (frame);
  fraction = aug->total / total;

  text = g_strdup_printf ("%.2lf", fraction*100.);

  child = gtk_list_item_get_child (list_item);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (child), fraction);
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (child), text);
}

static void
self_setup (GtkSignalListItemFactory *factory,
            GtkListItem              *list_item,
            SysprofCallgraph         *callgraph)
{
  GtkWidget *child;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));
  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  child = g_object_new (GTK_TYPE_PROGRESS_BAR,
                        "show-text", TRUE,
                        NULL);
  gtk_list_item_set_child (list_item, child);
}

static void
self_bind (GtkSignalListItemFactory *factory,
           GtkListItem              *list_item,
           gpointer                  user_data)
{
  g_autofree char *text = NULL;
  SysprofCallgraphFrame *frame;
  GtkTreeListRow *row;
  GtkWidget *child;
  double fraction;
  Augment *aug;

  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));
  g_assert (GTK_IS_LIST_ITEM (list_item));

  row = gtk_list_item_get_item (list_item);
  frame = gtk_tree_list_row_get_item (row);
  aug = sysprof_callgraph_frame_get_augment (frame);
  fraction = aug->size / total;

  text = g_strdup_printf ("%.2lf", fraction*100.);

  child = gtk_list_item_get_child (list_item);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (child), fraction);
  gtk_progress_bar_set_text (GTK_PROGRESS_BAR (child), text);
}

static GListModel *
create_model_func (gpointer item,
                   gpointer user_data)
{
  if (g_list_model_get_n_items (G_LIST_MODEL (item)) > 0)
    return g_object_ref (G_LIST_MODEL (item));
  return NULL;
}

static int
sort_by_self (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
  SysprofCallgraphFrame *frame_a = (SysprofCallgraphFrame *)a;
  SysprofCallgraphFrame *frame_b = (SysprofCallgraphFrame *)b;
  Augment *aug_a = sysprof_callgraph_frame_get_augment (frame_a);
  Augment *aug_b = sysprof_callgraph_frame_get_augment (frame_b);
  double self_a = aug_a->size / total;
  double self_b = aug_b->size / total;

  if (self_a < self_b)
    return -1;
  else if (self_a > self_b)
    return 1;
  else
    return 0;
}

static int
sort_by_total (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  SysprofCallgraphFrame *frame_a = (SysprofCallgraphFrame *)a;
  SysprofCallgraphFrame *frame_b = (SysprofCallgraphFrame *)b;
  Augment *aug_a = sysprof_callgraph_frame_get_augment (frame_a);
  Augment *aug_b = sysprof_callgraph_frame_get_augment (frame_b);
  double total_a = aug_a->total / total;
  double total_b = aug_b->total / total;

  if (total_a < total_b)
    return -1;
  else if (total_a > total_b)
    return 1;
  else
    return 0;
}

static void
show_callgraph (SysprofCallgraph *callgraph)
{
  g_autofree char *name = g_path_get_basename (filename);
  g_autoptr(GtkTreeListModel) tree = NULL;
  g_autoptr(GtkMultiSelection) model = NULL;
  g_autoptr(SysprofCallgraphFrame) root = NULL;
  GtkColumnViewColumn *column;
  GtkScrolledWindow *scroller;
  GtkColumnView *column_view;
  GtkListItemFactory *factory;
  g_autoptr(GtkTreeListRowSorter) sorter = NULL;
  g_autoptr(GtkSortListModel) sort_model = NULL;
  g_autoptr(GtkCustomSorter) self_sorter = NULL;
  g_autoptr(GtkCustomSorter) total_sorter = NULL;
  GtkSorter *column_sorter = NULL;
  GtkWindow *window;
  Augment *aug;

  root = g_list_model_get_item (G_LIST_MODEL (callgraph), 0);
  aug = sysprof_callgraph_frame_get_augment (root);
  total = aug->total;

  self_sorter = gtk_custom_sorter_new (sort_by_self, NULL, NULL);
  total_sorter = gtk_custom_sorter_new (sort_by_total, NULL, NULL);

  window = g_object_new (GTK_TYPE_WINDOW,
                         "default-width", 1024,
                         "default-height", 800,
                         "title", name,
                         NULL);
  scroller = g_object_new (GTK_TYPE_SCROLLED_WINDOW, NULL);
  gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (scroller));
  column_view = g_object_new (GTK_TYPE_COLUMN_VIEW, NULL);
  gtk_widget_add_css_class (GTK_WIDGET (column_view), "data-table");
  gtk_scrolled_window_set_child (scroller, GTK_WIDGET (column_view));

  tree = gtk_tree_list_model_new (g_object_ref (G_LIST_MODEL (callgraph)),
                                  FALSE, FALSE, create_model_func, NULL, NULL);
  column_sorter = gtk_column_view_get_sorter (column_view);
  sorter = gtk_tree_list_row_sorter_new (g_object_ref (column_sorter));
  sort_model = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (tree)), g_object_ref (GTK_SORTER (sorter)));
  model = gtk_multi_selection_new (g_object_ref (G_LIST_MODEL (sort_model)));
  gtk_column_view_set_model (column_view, GTK_SELECTION_MODEL (model));

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (factory, "setup", G_CALLBACK (function_setup), callgraph, 0);
  g_signal_connect_object (factory, "bind", G_CALLBACK (function_bind), callgraph, 0);
  column = gtk_column_view_column_new ("Function", factory);
  gtk_column_view_column_set_expand (column, TRUE);
  gtk_column_view_append_column (column_view, column);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (factory, "setup", G_CALLBACK (self_setup), callgraph, 0);
  g_signal_connect (factory, "bind", G_CALLBACK (self_bind), NULL);
  column = gtk_column_view_column_new ("Self", factory);
  gtk_column_view_column_set_expand (column, FALSE);
  gtk_column_view_column_set_sorter (column, GTK_SORTER (self_sorter));
  gtk_column_view_append_column (column_view, column);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (factory, "setup", G_CALLBACK (total_setup), callgraph, 0);
  g_signal_connect (factory, "bind", G_CALLBACK (total_bind), NULL);
  column = gtk_column_view_column_new ("Total", factory);
  gtk_column_view_column_set_expand (column, FALSE);
  gtk_column_view_column_set_sorter (column, GTK_SORTER (total_sorter));
  gtk_column_view_append_column (column_view, column);
  gtk_column_view_sort_by_column (column_view, column, GTK_SORT_DESCENDING);

  g_signal_connect_swapped (window,
                            "close-request",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);
  gtk_window_present (window);
}

static void
callgraph_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = SYSPROF_DOCUMENT (object);
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCallgraph) callgraph = sysprof_document_callgraph_finish (document, result, &error);

  g_print ("Done.\n");

  g_assert_no_error (error);
  g_assert_true (SYSPROF_IS_CALLGRAPH (callgraph));

  show_callgraph (callgraph);
}

static void
augment_cb (SysprofCallgraph     *callgraph,
            SysprofCallgraphNode *node,
            SysprofDocumentFrame *frame,
            gpointer              user_data)
{
  Augment *aug;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_SAMPLE (frame));
  g_assert (user_data == NULL);

  aug = sysprof_callgraph_get_augment (callgraph, node);
  aug->size += 1;

  for (; node; node = sysprof_callgraph_node_parent (node))
    {
      aug = sysprof_callgraph_get_augment (callgraph, node);
      aug->total += 1;
    }
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- show a callgraph");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GError) error = NULL;

  gtk_init ();
  sysprof_clock_init ();

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (argc != 2)
    {
      g_print ("usage: %s [OPTIONS] CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  main_loop = g_main_loop_new (NULL, FALSE);

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

  loader = sysprof_document_loader_new (filename);
  sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));

  g_print ("Loading %s, ignoring embedded symbols...\n", filename);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  g_print ("Loaded and symbolized. Generating callgraph...\n");
  samples = sysprof_document_list_samples (document);
  sysprof_document_callgraph_async (document,
                                    samples,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    NULL,
                                    callgraph_cb,
                                    main_loop);

  g_main_loop_run (main_loop);

  return 0;
}
