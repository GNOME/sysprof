/* sysprof-flamegraph.c
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

#include <stdio.h>
#include <string.h>

#include <libdex.h>

#include <sysprof.h>

#include "sysprof-callgraph-private.h"
#include "sysprof-document-loader-private.h"
#include "sysprof-symbol-private.h"

static GMainLoop *main_loop;
static int exit_code = -1;
static gboolean opt_debuginfod;
static guint progress_line_len;

static const GOptionEntry options[] = {
  { "debuginfod", 0, 0, G_OPTION_ARG_NONE, &opt_debuginfod, "Download missing symbols with debuginfod", NULL },
  { NULL }
};

typedef struct
{
  guint size;
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
  aug->size++;
}

static void
load_callgraph_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  SysprofCallgraph *callgraph;
  GError *error = NULL;

  if ((callgraph = sysprof_document_callgraph_finish (SYSPROF_DOCUMENT (object),
                                                      result,
                                                      &error)))
    dex_promise_resolve_object (promise, callgraph);
  else
    dex_promise_reject (promise, error);
}

static DexFuture *
load_callgraph (SysprofDocument *document)
{
  g_autoptr(GListModel) samples = NULL;
  DexPromise *promise;

  g_assert (SYSPROF_IS_DOCUMENT (document));

  samples = sysprof_document_list_samples (document);
  promise = dex_promise_new ();

  sysprof_document_callgraph_async (document,
                                    SYSPROF_CALLGRAPH_FLAGS_NONE,
                                    samples,
                                    sizeof (Augment),
                                    augment_cb, NULL, NULL,
                                    dex_promise_get_cancellable (promise),
                                    load_callgraph_cb,
                                    dex_ref (promise));

  return DEX_FUTURE (promise);
}

static SysprofSymbolizer *
create_symbolizer (void)
{
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;

  multi = sysprof_multi_symbolizer_new ();

  sysprof_multi_symbolizer_take (multi, sysprof_bundled_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

#if HAVE_DEBUGINFOD
  if (opt_debuginfod)
    {
      g_autoptr(SysprofSymbolizer) debuginfod = NULL;
      g_autoptr(GError) error = NULL;

      if (!(debuginfod = sysprof_debuginfod_symbolizer_new (&error)))
        g_warning ("Failed to create debuginfod symbolizer: %s", error->message);
      else
        sysprof_multi_symbolizer_take (multi, g_steal_pointer (&debuginfod));
    }
#endif

  return SYSPROF_SYMBOLIZER (g_steal_pointer (&multi));
}

static void
clear_progress_bar (void)
{
  if (progress_line_len == 0)
    return;

  g_printerr ("\r%*s\r", progress_line_len, "");
  fflush (stderr);

  progress_line_len = 0;
}

static void
print_progress_bar (SysprofDocumentTask *task)
{
  g_autofree char *message = NULL;
  g_autofree char *title = NULL;
  g_autofree char *name = NULL;
  g_autofree char *line = NULL;
  const char full[] = "##############################";
  const guint width = sizeof full - 1;
  double progress;
  guint filled;
  guint percent;

  g_assert (SYSPROF_IS_DOCUMENT_TASK (task));

  progress = sysprof_document_task_get_progress (task);
  progress = CLAMP (progress, 0., 1.);
  filled = progress * width;
  percent = progress * 100;
  title = sysprof_document_task_dup_title (task);
  message = sysprof_document_task_dup_message (task);

  if (message != NULL)
    name = g_path_get_basename (message);

  line = g_strdup_printf ("%s [%.*s%*s] %3u%% %s",
                          title ? title : "Downloading Symbols",
                          filled, full,
                          width - filled, "",
                          percent,
                          name ? name : "");

  g_printerr ("\r%s%*s", line, MAX (0, (int)progress_line_len - (int)strlen (line)), "");
  fflush (stderr);

  progress_line_len = strlen (line);
}

static void
render_progress_bar (GListModel *tasks)
{
  g_autoptr(SysprofDocumentTask) task = NULL;

  g_assert (G_IS_LIST_MODEL (tasks));

  if (g_list_model_get_n_items (tasks) == 0)
    {
      clear_progress_bar ();
      return;
    }

  task = g_list_model_get_item (tasks, 0);
  print_progress_bar (task);
}

static void
task_notify_cb (SysprofDocumentTask *task,
                GParamSpec          *pspec,
                gpointer             user_data)
{
  GListModel *tasks = user_data;

  g_assert (SYSPROF_IS_DOCUMENT_TASK (task));
  g_assert (G_IS_LIST_MODEL (tasks));

  render_progress_bar (tasks);
}

static void
tasks_items_changed_cb (GListModel *tasks,
                        guint       position,
                        guint       removed,
                        guint       added,
                        gpointer    user_data)
{
  g_assert (G_IS_LIST_MODEL (tasks));

  for (guint i = position; i < position + added; i++)
    {
      g_autoptr(SysprofDocumentTask) task = g_list_model_get_item (tasks, i);

      g_signal_connect_object (task,
                               "notify",
                               G_CALLBACK (task_notify_cb),
                               tasks,
                               0);
    }

  render_progress_bar (tasks);
}

static GListModel *
monitor_debuginfod_progress (SysprofDocumentLoader *loader)
{
  g_autoptr(GListModel) tasks = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));

  if (!opt_debuginfod)
    return NULL;

  tasks = sysprof_document_loader_list_tasks (loader);
  g_signal_connect (tasks,
                    "items-changed",
                    G_CALLBACK (tasks_items_changed_cb),
                    NULL);

  return g_steal_pointer (&tasks);
}

static void
append_frame_name (GString    *stack,
                   const char *name)
{
  g_assert (stack != NULL);

  if (name == NULL || name[0] == 0)
    name = "[unknown]";

  for (const char *iter = name; *iter; iter = g_utf8_next_char (iter))
    {
      gunichar ch = g_utf8_get_char_validated (iter, -1);

      if (ch == (gunichar)-1 || ch == (gunichar)-2)
        break;

      switch (ch)
        {
        case ';':
        case '\n':
        case '\r':
          g_string_append_c (stack, ' ');
          break;

        default:
          g_string_append_unichar (stack, ch);
          break;
        }
    }
}

static void
print_flamegraph_stacks (SysprofCallgraph     *callgraph,
                         SysprofCallgraphNode *node,
                         GString              *stack)
{
  const Augment *aug;
  gsize len;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (stack != NULL);

  len = stack->len;
  aug = sysprof_callgraph_get_augment (callgraph, node);

  if (node->summary != NULL && node->summary->symbol != NULL)
    {
      if (stack->len > 0)
        g_string_append_c (stack, ';');

      append_frame_name (stack, node->summary->symbol->name);
    }

  if (stack->len > 0 && aug != NULL && aug->size > 0)
    g_print ("%s %u\n", stack->str, aug->size);

  for (SysprofCallgraphNode *child = node->children; child != NULL; child = child->next)
    print_flamegraph_stacks (callgraph, child, stack);

  g_string_truncate (stack, len);
}

static DexFuture *
sysprof_flamegraph_fiber (gpointer data)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(SysprofSymbolizer) symbolizer = NULL;
  g_autoptr(GListModel) tasks = NULL;
  g_autoptr(GString) stack = NULL;
  g_autoptr(GError) error = NULL;
  const char *filename = data;

  g_assert (filename != NULL);

  loader = sysprof_document_loader_new (filename);
  symbolizer = create_symbolizer ();
  sysprof_document_loader_set_symbolizer (loader, symbolizer);
  tasks = monitor_debuginfod_progress (loader);

  if (!(document = dex_await_object (_sysprof_document_loader_load (loader), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  if (!(callgraph = dex_await_object (load_callgraph (document), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  stack = g_string_new (NULL);
  clear_progress_bar ();
  print_flamegraph_stacks (callgraph, &callgraph->root, stack);

  exit_code = EXIT_SUCCESS;

  return dex_future_new_true ();
}

static DexFuture *
sysprof_flamegraph_finally_cb (DexFuture *future,
                               gpointer   user_data)
{
  g_autoptr(GError) error = NULL;

  if (!dex_await (dex_ref (future), &error))
    {
      clear_progress_bar ();
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

  g_set_prgname ("sysprof-flamegraph");
  g_set_application_name ("sysprof-flamegraph");

  main_loop = g_main_loop_new (NULL, FALSE);

  context = g_option_context_new ("CAPTURE -- Dump stacks for flamegraph.pl");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (argc != 2)
    {
      g_printerr ("usage: %s CAPTURE\n", argv[0]);
      return EXIT_FAILURE;
    }

  dex_future_disown (dex_future_finally (dex_scheduler_spawn (NULL, 0,
                                                              sysprof_flamegraph_fiber,
                                                              g_strdup (argv[1]),
                                                              g_free),
                                         sysprof_flamegraph_finally_cb,
                                         NULL, NULL));

  if (exit_code == -1)
    g_main_loop_run (main_loop);

  g_clear_pointer (&main_loop, g_main_loop_unref);

  return exit_code;
}
