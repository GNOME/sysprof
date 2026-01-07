/* top-offenders.c
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-callgraph-private.h"

typedef struct _Augment
{
  guint64 hits;
} Augment;

typedef struct _Offender
{
  SysprofCallgraphFrame *frame;
  guint64 hits;
  GPtrArray *path;
} Offender;

static int filter_pid = -1;
static int filter_tid = -1;
static int top_n = 10;

static const GOptionEntry entries[] = {
  { "process", 'p', 0, G_OPTION_ARG_INT, &filter_pid, "Filter by process ID", "PID" },
  { "thread", 't', 0, G_OPTION_ARG_INT, &filter_tid, "Filter by thread ID", "TID" },
  { "num", 'n', 0, G_OPTION_ARG_INT, &top_n, "Number of top offenders to show", "N" },
  { 0 }
};

static GListModel *
filter_samples (SysprofDocument *document)
{
  g_autoptr(GListModel) all_samples = NULL;
  g_autoptr(GListStore) filtered = NULL;
  guint n_items;

  all_samples = sysprof_document_list_samples (document);
  filtered = g_list_store_new (SYSPROF_TYPE_DOCUMENT_SAMPLE);

  n_items = g_list_model_get_n_items (all_samples);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentSample) sample = g_list_model_get_item (all_samples, i);
      int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (sample));
      int tid = sysprof_document_traceable_get_thread_id (SYSPROF_DOCUMENT_TRACEABLE (sample));

      if (filter_pid != -1 && pid != filter_pid)
        continue;

      if (filter_tid != -1 && tid != filter_tid)
        continue;

      g_list_store_append (filtered, sample);
    }

  return G_LIST_MODEL (g_steal_pointer (&filtered));
}

static void
augment_cb (SysprofCallgraph     *callgraph,
            SysprofCallgraphNode *node,
            SysprofDocumentFrame *frame,
            gboolean              summarize,
            gpointer              user_data)
{
  Augment *aug;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_SAMPLE (frame));

  aug = sysprof_callgraph_get_augment (callgraph, node);
  aug->hits += 1;

  for (SysprofCallgraphNode *iter = node->parent; iter; iter = iter->parent)
    {
      aug = sysprof_callgraph_get_augment (callgraph, iter);
      aug->hits += 1;
    }
}

static void
collect_leaf_nodes (SysprofCallgraphFrame *frame,
                    GPtrArray            *offenders,
                    GPtrArray            *current_path)
{
  SysprofSymbol *symbol;
  const char *symbol_name;

  if (frame == NULL)
    return;

  symbol = sysprof_callgraph_frame_get_symbol (frame);
  symbol_name = symbol ? sysprof_symbol_get_name (symbol) : NULL;

  /* Add current frame to path */
  if (symbol_name)
    g_ptr_array_add (current_path, g_object_ref (frame));

  /* Check if this is a leaf node */
  if (sysprof_callgraph_frame_is_leaf (frame))
    {
      Augment *aug = sysprof_callgraph_frame_get_augment (frame);
      Offender *offender = g_new0 (Offender, 1);

      offender->frame = g_object_ref (frame);
      offender->hits = aug ? aug->hits : 0;
      offender->path = g_ptr_array_new_with_free_func (g_object_unref);

      /* Copy current path */
      for (guint i = 0; i < current_path->len; i++)
        {
          SysprofCallgraphFrame *path_frame = g_ptr_array_index (current_path, i);
          g_ptr_array_add (offender->path, g_object_ref (path_frame));
        }

      g_ptr_array_add (offenders, offender);
    }
  else
    {
      /* Recurse into children */
      guint n_children = g_list_model_get_n_items (G_LIST_MODEL (frame));
      for (guint i = 0; i < n_children; i++)
        {
          g_autoptr(SysprofCallgraphFrame) child = g_list_model_get_item (G_LIST_MODEL (frame), i);
          collect_leaf_nodes (child, offenders, current_path);
        }
    }

  /* Remove current frame from path when backtracking */
  if (symbol_name && current_path->len > 0)
    {
      /* g_ptr_array_remove_index will call the free_func (g_object_unref) automatically */
      g_ptr_array_remove_index (current_path, current_path->len - 1);
    }
}

static gint
compare_offenders (gconstpointer a,
                   gconstpointer b)
{
  const Offender *off_a = *(const Offender **)a;
  const Offender *off_b = *(const Offender **)b;

  if (off_a->hits < off_b->hits)
    return 1;
  else if (off_a->hits > off_b->hits)
    return -1;
  else
    return 0;
}

static void
offender_free (Offender *offender)
{
  if (offender)
    {
      g_clear_object (&offender->frame);
      if (offender->path)
        g_ptr_array_unref (offender->path);
      g_free (offender);
    }
}

static void
print_stack_trace (Offender *offender)
{
  guint frame_num = 0;

  /* Print path from leaf to root (frame 00 is the leaf where sample occurred) */
  for (gint i = offender->path->len - 1; i >= 0; i--)
    {
      SysprofCallgraphFrame *path_frame = g_ptr_array_index (offender->path, i);
      SysprofSymbol *symbol = sysprof_callgraph_frame_get_symbol (path_frame);
      Augment *aug = sysprof_callgraph_frame_get_augment (path_frame);
      const char *symbol_name = symbol ? sysprof_symbol_get_name (symbol) : "?";
      guint64 hits = aug ? aug->hits : 0;

      g_print ("%3u: %7" G_GUINT64_FORMAT ": %s\n", frame_num++, hits, symbol_name);
    }
}

static void
callgraph_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  SysprofDocument *document = SYSPROF_DOCUMENT (object);
  GMainLoop *main_loop = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GPtrArray) offenders = NULL;
  g_autoptr(SysprofCallgraphFrame) root_frame = NULL;
  guint n_to_print;

  callgraph = sysprof_document_callgraph_finish (document, result, &error);
  if (error)
    {
      g_printerr ("Failed to build callgraph: %s\n", error->message);
      g_main_loop_quit (main_loop);
      return;
    }

  g_assert_true (SYSPROF_IS_CALLGRAPH (callgraph));

  offenders = g_ptr_array_new_with_free_func ((GDestroyNotify)offender_free);

  root_frame = g_list_model_get_item (G_LIST_MODEL (callgraph), 0);
  if (root_frame)
    {
      g_autoptr(GPtrArray) current_path = g_ptr_array_new_with_free_func (g_object_unref);
      collect_leaf_nodes (root_frame, offenders, current_path);
    }

  /* Sort by hits (descending) */
  g_ptr_array_sort (offenders, compare_offenders);

  /* Print header */
  g_print ("FRM:    HITS: SYMBOL\n");

  /* Print top N offenders */
  n_to_print = MIN (top_n, offenders->len);
  for (guint i = 0; i < n_to_print; i++)
    {
      Offender *offender = g_ptr_array_index (offenders, i);
      print_stack_trace (offender);
      if (i < n_to_print - 1)
        g_print ("\n\n\n");
    }

  g_main_loop_quit (main_loop);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- find top offenders in syscap file");
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, FALSE);
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  multi = sysprof_multi_symbolizer_new ();
  sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to load document: %s\n", error->message);
      return 1;
    }

  /* Not very efficient, but easy for now */
  samples = filter_samples (document);

  flags |= SYSPROF_CALLGRAPH_FLAGS_LEFT_HEAVY;
  flags |= SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS;
  flags |= SYSPROF_CALLGRAPH_FLAGS_IGNORE_PROCESS_0;

  sysprof_document_callgraph_async (document,
                                    flags,
                                    samples,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    NULL,
                                    callgraph_cb,
                                    main_loop);

  g_main_loop_run (main_loop);

  return 0;
}
