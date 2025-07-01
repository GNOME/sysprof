/* sysprof-diff.c
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

#include <libdex.h>

#include <sysprof.h>

#include "sysprof-callgraph-private.h"
#include "sysprof-symbol-private.h"

static GMainLoop *main_loop;
static int exit_code = -1;
static char empty[1024];
static const GOptionEntry options[] = {
  { NULL }
};

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

static DexFuture *
load_callgraph (SysprofDocument         *document,
                SysprofCallgraphFlags    flags,
                GListModel              *traceables,
                gsize                    augment_size,
                SysprofAugmentationFunc  augment_func,
                gpointer                 augment_func_data,
                GDestroyNotify           augment_func_data_destroy)
{
  DexPromise *promise;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_MODEL (traceables));

  promise = dex_promise_new ();

  sysprof_document_callgraph_async (document,
                                    flags,
                                    traceables,
                                    augment_size,
                                    augment_func,
                                    augment_func_data,
                                    augment_func_data_destroy,
                                    dex_promise_get_cancellable (promise),
                                    load_callgraph_cb,
                                    dex_ref (promise));

  return DEX_FUTURE (promise);
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

static int
compare (SysprofCallgraphNode *a,
         SysprofCallgraphNode *b)
{
  if (a == b)
    return 0;

  if (a != NULL && b == NULL)
    return -1;

  if (a == NULL && b != NULL)
    return 1;

  return strcmp (a->summary->symbol->name,
                 b->summary->symbol->name);
}

static void
diff (SysprofCallgraphNode *left,
      SysprofCallgraphNode *right,
      guint                 depth)
{
  const Augment *l_aug = left ? (const Augment *)&left->augment[0] : NULL;
  const Augment *r_aug = right ? (const Augment *)&right->augment[0] : NULL;
  int l_total = l_aug ? l_aug->total : 0;
  int r_total = r_aug ? r_aug->total : 0;
  int change = r_total - l_total;
  const char *name = left ? left->summary->symbol->name : right->summary->symbol->name;
  SysprofCallgraphNode *l_iter;
  SysprofCallgraphNode *r_iter;

  g_print ("%6u | %6u | %+6d | ", l_total, r_total, change);
  if (depth > 0)
    write (STDOUT_FILENO, empty, MIN (sizeof empty, depth*2));
  g_print ("%s\n", name);

  l_iter = left ? left->children : NULL;
  r_iter = right ? right->children : NULL;

  while (l_iter || r_iter)
    {
      while (r_iter && compare (r_iter, l_iter) < 0)
        {
          diff (NULL, r_iter, depth+1);
          r_iter = r_iter->next;
        }

      if (l_iter)
        {
          if (r_iter && compare (l_iter, r_iter) == 0)
            diff (l_iter, r_iter, depth+1);
          else
            diff (l_iter, NULL, depth+1);

          l_iter = l_iter->next;
        }
    }
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
sysprof_diff_fiber (gpointer data)
{
  const char * const *filenames = data;
  g_autoptr(SysprofDocumentLoader) loader1 = NULL;
  g_autoptr(SysprofDocumentLoader) loader2 = NULL;
  g_autoptr(SysprofDocument) document1 = NULL;
  g_autoptr(SysprofDocument) document2 = NULL;
  g_autoptr(SysprofCallgraph) callgraph1 = NULL;
  g_autoptr(SysprofCallgraph) callgraph2 = NULL;
  g_autoptr(GListModel) traceables1 = NULL;
  g_autoptr(GListModel) traceables2 = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphFlags flags = 0;

  g_assert (filenames != NULL);
  g_assert (filenames[0] != NULL);
  g_assert (filenames[1] != NULL);

  loader1 = sysprof_document_loader_new (filenames[0]);
  loader2 = sysprof_document_loader_new (filenames[1]);

  g_printerr ("Loading %s...\n", filenames[0]);
  if (!(document1 = dex_await_object (load_document (loader1), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  traceables1 = sysprof_document_list_samples (document1);

  if (!(callgraph1 = dex_await_object (load_callgraph (document1, flags, traceables1, sizeof (Augment), augment_cb, NULL, NULL), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  g_printerr ("Loading %s...\n", filenames[1]);
  if (!(document2 = dex_await_object (load_document (loader2), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  traceables2 = sysprof_document_list_samples (document2);

  if (!(callgraph2 = dex_await_object (load_callgraph (document2, flags, traceables2, sizeof (Augment), augment_cb, NULL, NULL), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  g_print ("BEFORE | AFTER  | CHANGE | FUNCTION\n");
  g_print ("-------|--------|--------|---------\n");
  diff (&callgraph1->root, &callgraph2->root, 0);

  exit_code = EXIT_SUCCESS;

  g_main_loop_quit (main_loop);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_diff_catch_error_cb (DexFuture *future,
                             gpointer   user_data)
{
  g_autoptr(GError) error = NULL;

  dex_await (dex_ref (future), &error);

  g_printerr ("Error: %s\n", error->message);

  exit_code = EXIT_FAILURE;

  g_main_loop_quit (main_loop);

  return NULL;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  const char *copy[3];

  sysprof_clock_init ();
  dex_init ();

  g_set_prgname ("sysprof-diff");
  g_set_application_name ("sysprof-diff");

  main_loop = g_main_loop_new (NULL, FALSE);

  context = g_option_context_new ("BEFORE_CAPTURE AFTER_CAPTURE -- Generate a callgraph diff between two captures");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (argc < 3)
    {
      g_printerr ("usage: %s BEFORE_CAPTURE AFTER_CAPTURE\n", argv[0]);
      return EXIT_FAILURE;
    }

  copy[0] = argv[1];
  copy[1] = argv[2];
  copy[2] = NULL;

  memset (empty, ' ', sizeof empty);

  dex_future_disown (
      dex_future_catch (
          dex_scheduler_spawn (NULL, 0,
                               sysprof_diff_fiber,
                               g_strdupv ((char **)copy),
                               (GDestroyNotify)g_strfreev),
          sysprof_diff_catch_error_cb,
          NULL, NULL));

  if (exit_code == -1)
    g_main_loop_run (main_loop);

  return exit_code;
}
