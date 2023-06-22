/* test-callgraph.c
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

#include <adwaita.h>
#include <gtk/gtk.h>
#include <sysprof-analyze.h>
#include <sysprof-gtk.h>

static GMainLoop *main_loop;
static char *kallsyms_path;
static char *filename;
static gboolean include_threads;
static const GOptionEntry entries[] = {
  { "kallsyms", 'k', 0, G_OPTION_ARG_FILENAME, &kallsyms_path, "The path to kallsyms to use for decoding", "PATH" },
  { "threads", 't', 0, G_OPTION_ARG_NONE, &include_threads, "Include threads in the callgraph" },
  { 0 }
};

static void
load_cb (GObject      *object,
         GAsyncResult *result,
         gpointer      user_data)
{
  g_autoptr(GtkWidget) view = user_data;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;

  document = sysprof_document_loader_load_finish (SYSPROF_DOCUMENT_LOADER (object), result, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);

  model = sysprof_document_list_samples (document);

  g_object_set (view,
                "document", document,
                "traceables", model,
                NULL);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- show a callgraph");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCallgraphView *view;
  GtkWidget *box;
  GtkWidget *hbox;
  GtkWidget *status;
  GtkWidget *progress;
  GtkWidget *message;
  GtkWidget *threads;
  GtkWindow *window;

  sysprof_clock_init ();

  gtk_init ();
  adw_init ();

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

  window = g_object_new (GTK_TYPE_WINDOW,
                         "default-width", 800,
                         "default-height", 600,
                         NULL);
  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      "spacing", 12,
                      NULL);
  hbox = g_object_new (GTK_TYPE_BOX,
                       "spacing", 6,
                       NULL);
  gtk_box_append (GTK_BOX (box), hbox);
  gtk_box_append (GTK_BOX (hbox), gtk_label_new ("Show Threads"));
  threads = g_object_new (GTK_TYPE_SWITCH,
                          "active", include_threads,
                          NULL);
  gtk_box_append (GTK_BOX (hbox), threads);
  view = g_object_new (SYSPROF_TYPE_WEIGHTED_CALLGRAPH_VIEW,
                       "include-threads", include_threads,
                       "vexpand", TRUE,
                       NULL);
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (view));
  g_signal_connect_swapped (window,
                            "close-request",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);
  status = g_object_new (GTK_TYPE_BOX,
                         "spacing", 6,
                         NULL);
  message = g_object_new (GTK_TYPE_LABEL,
                          "xalign", 1.f,
                          "hexpand", TRUE,
                          NULL);
  progress = g_object_new (GTK_TYPE_PROGRESS_BAR,
                           NULL);
  gtk_box_append (GTK_BOX (status), message);
  gtk_box_append (GTK_BOX (status), progress);
  gtk_box_append (GTK_BOX (box), status);
  gtk_window_set_child (window, box);
  gtk_window_present (window);

  g_object_bind_property (threads, "active", view, "include-threads", 0);
  g_object_bind_property (loader, "message", message, "label", 0);
  g_object_bind_property (loader, "fraction", progress, "fraction", 0);

  sysprof_document_loader_load_async (loader, NULL, load_cb, g_object_ref (view));

  g_main_loop_run (main_loop);

  return 0;
}
