/* sysprof-cat.c
 *
 * Copyright 2024 Igalia S.L.
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
 * Authors: Georges Basile Stavracas Neto <feaneron@igalia.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <libdex.h>

#include <sysprof.h>

#include "sysprof-callgraph-private.h"
#include "sysprof-symbol-private.h"

static GMainLoop *main_loop;
static int exit_code = -1;
static gboolean opt_skip_callgraph = FALSE;
static gboolean opt_skip_marks = FALSE;
static gboolean opt_skip_counters = FALSE;
static gboolean opt_skip_metadata = FALSE;
static const GOptionEntry options[] = {
  { "no-callgraph", 0, 0, G_OPTION_ARG_NONE, &opt_skip_callgraph, "Do not dump the callgraph" , NULL },
  { "no-counters", 0, 0, G_OPTION_ARG_NONE, &opt_skip_counters, "Do not dump counters" , NULL },
  { "no-marks", 0, 0, G_OPTION_ARG_NONE, &opt_skip_marks, "Do not dump marks" , NULL },
  { "no-metadata", 0, 0, G_OPTION_ARG_NONE, &opt_skip_metadata, "Do not dump capture metadata" , NULL },
  { NULL }
};

static inline void
begin_document (SysprofDocument *document)
{
  const SysprofTimeSpan *timespan;
  char *aux;

  g_print ("document {\n");
  if ((aux = sysprof_document_dup_title (document)))
    {
      g_autofree char *markup = g_markup_escape_text (aux, -1);
      g_print ("  title: \"%s\";\n", markup);
      g_clear_pointer (&aux, g_free);
    }

  if ((aux = sysprof_document_dup_subtitle (document)))
    {
      g_autofree char *markup = g_markup_escape_text (aux, -1);
      g_print ("  subtitle: \"%s\";\n", markup);
      g_clear_pointer (&aux, g_free);
    }

  timespan = sysprof_document_get_time_span (document);
  g_print ("  timespan: %" G_GINT64_FORMAT " %" G_GINT64_FORMAT ";\n",
           timespan->begin_nsec,
           timespan->end_nsec);

}

typedef struct
{
  guint size;
  guint total;
} Augment;

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
  g_assert (user_data == NULL);

  aug = sysprof_callgraph_get_augment (callgraph, node);
  aug->size += 1;
  aug->total += 1;

  for (SysprofCallgraphNode *iter = node->parent; iter; iter = iter->parent)
    {
      aug = sysprof_callgraph_get_augment (callgraph, iter);
      aug->total += 1;
    }
}

static void
load_callgraph_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  SysprofCallgraph *callgraph;
  GError *error = NULL;

  if ((callgraph = sysprof_document_callgraph_finish (SYSPROF_DOCUMENT (object), result, &error)))
    dex_promise_resolve_object (promise, callgraph);
  else
    dex_promise_reject (promise, error);
}

static void
print_node_recursively (SysprofCallgraphNode *node,
                        int                   depth)
{
  const Augment *aug;

  if (!node)
    return;

  aug = (const Augment *) &node->augment[0];

  g_print ("%*cnode {\n", depth * 2, ' ');
  g_print ("%*csymbol: \"%s\",\n", (depth + 1) * 2, ' ', node->summary->symbol->name);
  g_print ("%*cself: %u,\n", (depth + 1) * 2, ' ', aug->size);
  g_print ("%*ctotal: %u,\n", (depth + 1) * 2, ' ', aug->total);

  for (SysprofCallgraphNode *child = node->children; child != NULL; child = child->next)
    {
      if (!child->prev)
        g_print ("\n");

      print_node_recursively (child, depth + 1);
    }

  g_print ("%*c}\n", depth * 2, ' ');
}

static void
dump_callgraph (SysprofDocument *document)
{
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GListModel) traceables = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;
  DexPromise *promise;

  if (opt_skip_callgraph)
    return;

  traceables = sysprof_document_list_samples (document);

  promise = dex_promise_new ();

  sysprof_document_callgraph_async (document,
                                    flags,
                                    traceables,
                                    sizeof (Augment),
                                    augment_cb,
                                    NULL, NULL,
                                    dex_promise_get_cancellable (promise),
                                    load_callgraph_cb,
                                    dex_ref (promise));

  callgraph = dex_await_object (DEX_FUTURE (promise), &error);

  if (error)
    return;

  g_print ("\n");
  g_print ("  callgraph {\n");

  print_node_recursively (&callgraph->root, 2);

  g_print ("  }\n");
}

static void
dump_counters (SysprofDocument *document)
{
  g_autoptr(GListModel) counters = NULL;
  unsigned int n_counters;

  if (opt_skip_counters)
    return;

  counters = sysprof_document_list_counters (document);
  g_assert (G_IS_LIST_MODEL (counters));

  n_counters = g_list_model_get_n_items (counters);

  if (n_counters == 0)
    return;

  g_print ("\n");
  g_print ("  counters {\n");

  for (unsigned int i = 0; i < n_counters; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = NULL;
      unsigned int n_values;
      const char *aux;

      counter = g_list_model_get_item (counters, i);
      g_assert (SYSPROF_IS_DOCUMENT_COUNTER (counter));

      g_print ("    counter {\n");

      if ((aux = sysprof_document_counter_get_category (counter)))
        {
          g_autofree char *markup = g_markup_escape_text (aux, -1);
          g_print ("      category: \"%s\";\n", markup);
        }

      if ((aux = sysprof_document_counter_get_name (counter)))
        {
          g_autofree char *markup = g_markup_escape_text (aux, -1);
          g_print ("      name: \"%s\";\n", markup);
        }

      if ((aux = sysprof_document_counter_get_description (counter)))
        {
          g_autofree char *markup = g_markup_escape_text (aux, -1);
          g_print ("      description: \"%s\";\n", markup);
        }

      n_values = sysprof_document_counter_get_n_values (counter);
      if (n_values)
        {
          g_print ("\n");
          g_print ("      values {\n");
          for (unsigned int j = 0; j < n_values; j++)
            {
              g_autoptr(SysprofDocumentCounterValue) value = NULL;
              g_autofree char *formatted_value = NULL;

              value = g_list_model_get_item (G_LIST_MODEL (counter), j);
              g_assert (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (value));

              formatted_value = sysprof_document_counter_value_format (value);

              g_print ("        value {\n");
              g_print ("          time: %" G_GINT64_FORMAT ";\n", sysprof_document_counter_value_get_time (value));
              g_print ("          offset: %" G_GINT64_FORMAT ";\n", sysprof_document_counter_value_get_time (value));
              g_print ("          value: %s;\n", formatted_value);
              g_print ("        }\n");
            }
          g_print ("      }\n");
        }

      g_print ("    }\n");
    }

  g_print ("  }\n");
}

static void
dump_marks (SysprofDocument *document)
{
  g_autoptr(GListModel) groups = NULL;
  unsigned int n_groups;

  if (opt_skip_marks)
    return;

  groups = sysprof_document_catalog_marks (document);
  g_assert (G_IS_LIST_MODEL (groups));

  n_groups = g_list_model_get_n_items (groups);

  if (n_groups == 0)
    return;

  g_print ("\n");
  g_print ("  marks {\n");

  for (unsigned int i = 0; i < n_groups; i++)
    {
      g_autoptr(GListModel) catalogs = NULL;
      unsigned int n_catalogs;

      catalogs = g_list_model_get_item (groups, i);
      g_assert (G_IS_LIST_MODEL (catalogs));

      g_print ("\n");
      g_print ("    group {\n");

      n_catalogs = g_list_model_get_n_items (catalogs);
      for (unsigned int j = 0; j < n_catalogs; j++)
        {
          g_autoptr(SysprofMarkCatalog) catalog = NULL;
          unsigned int n_marks;
          const char *aux;

          catalog = g_list_model_get_item (catalogs, j);
          g_assert (SYSPROF_IS_MARK_CATALOG (catalog));

          if (j == 0 &&
              (aux = sysprof_mark_catalog_get_group (catalog)))
            {
              g_autofree char *markup = g_markup_escape_text (aux, -1);
              g_print ("      name: \"%s\";\n", markup);
              g_print ("\n");
            }

          n_marks = g_list_model_get_n_items (G_LIST_MODEL (catalog));
          for (unsigned int k = 0; k < n_marks; k++)
            {
              g_autoptr(SysprofDocumentMark) mark = NULL;

              mark = g_list_model_get_item (G_LIST_MODEL (catalog), k);
              g_assert (SYSPROF_IS_DOCUMENT_MARK (mark));

              g_print ("      mark {\n");

              if ((aux = sysprof_document_mark_get_name (mark)))
                {
                  g_autofree char *markup = g_markup_escape_text (aux, -1);
                  g_print ("        name: \"%s\";\n", markup);
                }

              if ((aux = sysprof_document_mark_get_message (mark)))
                {
                  g_autofree char *markup = g_markup_escape_text (aux, -1);
                  g_print ("        message: \"%s\";\n", markup);
                }

              g_print ("        duration: %" G_GINT64_FORMAT ";\n", sysprof_document_mark_get_duration (mark));
              g_print ("        end-time: %" G_GINT64_FORMAT ";\n", sysprof_document_mark_get_end_time (mark));
              g_print ("      }\n");
            }
        }

      g_print ("    }\n");
    }

  g_print ("  }\n");
}

static void
dump_metadata (SysprofDocument *document)
{
  g_autoptr(GListModel) metadatas = NULL;
  unsigned int n_metadatas;

  if (opt_skip_metadata)
    return;

  metadatas = sysprof_document_list_metadata (document);
  g_assert (G_IS_LIST_MODEL (metadatas));

  n_metadatas = g_list_model_get_n_items (metadatas);

  if (metadatas == 0)
    return;

  g_print ("\n");
  g_print ("  metadata {\n");

  for (unsigned int i = 0; i < n_metadatas; i++)
    {
      g_autoptr(SysprofDocumentMetadata) metadata = NULL;
      g_autofree char *value = NULL;
      g_autofree char *id = NULL;

      metadata = g_list_model_get_item (metadatas, i);
      g_assert (SYSPROF_IS_DOCUMENT_METADATA (metadata));

      id = g_markup_escape_text (sysprof_document_metadata_get_id (metadata), -1);
      value = g_markup_escape_text (sysprof_document_metadata_get_value (metadata), -1);

      g_print ("    \"%s\": \"%s\";\n", id, value);
    }

  g_print ("  }\n");
}

static inline void
end_document (SysprofDocument *document)
{
  g_print ("}\n");
}

static void
load_document_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  SysprofDocument *document;
  GError *error = NULL;

  if ((document = sysprof_document_loader_load_finish (SYSPROF_DOCUMENT_LOADER (object), result, &error)))
    dex_promise_resolve_object (promise, document);
  else
    dex_promise_reject (promise, error);
}

static DexFuture *
load_document (SysprofDocumentLoader *loader)
{
  DexPromise *promise = dex_promise_new ();
  sysprof_document_loader_load_async (loader, NULL, load_document_cb, dex_ref (promise));
  return DEX_FUTURE (promise);
}

static DexFuture *
sysprof_cat_fiber (gpointer data)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  const char *filename = data;

  g_assert (filename != NULL);

  loader = sysprof_document_loader_new (filename);

  if (!(document = dex_await_object (load_document (loader), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  begin_document (document);
  {
    dump_metadata (document);
    dump_callgraph (document);
    dump_marks (document);
    dump_counters (document);
  }
  end_document (document);

  exit_code = EXIT_SUCCESS;

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_cat_finally_cb (DexFuture *future,
                        gpointer   user_data)
{
  g_autoptr(GError) error = NULL;

  if (!dex_await (dex_ref (future), &error))
    {
      g_printerr ("Error: %s\n", error->message);
      exit_code = EXIT_FAILURE;
    }

  g_main_loop_quit (main_loop);

  return NULL;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;

  sysprof_clock_init ();
  dex_init ();

  g_set_prgname ("sysprof-cat");
  g_set_application_name ("sysprof-cat");

  main_loop = g_main_loop_new (NULL, FALSE);

  context = g_option_context_new ("CAPTURE -- Dump the contents of a capture");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      g_printerr ("usage: %s CAPTURE\n", argv[0]);
      return EXIT_FAILURE;
    }

  dex_future_disown (
      dex_future_finally (
          dex_scheduler_spawn (NULL, 0,
                               sysprof_cat_fiber,
                               g_strdup (argv[1]),
                               g_free),
          sysprof_cat_finally_cb,
          NULL, NULL));

  if (exit_code == -1)
    g_main_loop_run (main_loop);

  return exit_code;
}
