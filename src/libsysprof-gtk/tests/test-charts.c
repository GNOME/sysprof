/* test-charts.c
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
static char *filename;
static const GOptionEntry entries[] = {
  { 0 }
};

static gboolean
activate_layer_item_cb (SysprofChart      *chart,
                        SysprofChartLayer *layer,
                        gpointer           item,
                        SysprofDocument   *document)
{
  g_assert (SYSPROF_IS_CHART (chart));
  g_assert (SYSPROF_IS_CHART_LAYER (layer));
  g_assert (G_IS_OBJECT (item));
  g_assert (SYSPROF_IS_DOCUMENT (document));

  g_print ("Activated %s in layer '%s' [%s]\n",
           G_OBJECT_TYPE_NAME (item),
           sysprof_chart_layer_get_title (layer),
           G_OBJECT_TYPE_NAME (layer));

  if (SYSPROF_IS_DOCUMENT_FRAME (item))
    {
      g_print ("time_offset=%"G_GINT64_FORMAT" pid=%d\n",
               sysprof_document_frame_get_time_offset (item),
               sysprof_document_frame_get_pid (item));
    }

  if (SYSPROF_IS_DOCUMENT_TRACEABLE (item))
    {
      guint depth = sysprof_document_traceable_get_stack_depth (item);

      g_print ("Thread-Id: %u\n",
               sysprof_document_traceable_get_thread_id (item));
      g_print ("Stack Depth: %u\n", depth);

      if (depth <= 128)
        {
          SysprofSymbol *symbols[128];
          SysprofAddressContext final_context;
          guint n_symbols = G_N_ELEMENTS (symbols);

          n_symbols = sysprof_document_symbolize_traceable (document,
                                                            item,
                                                            symbols,
                                                            n_symbols,
                                                            &final_context);

          for (guint i = 0; i < n_symbols; i++)
            g_print ("  %s\n", sysprof_symbol_get_name (symbols[i]));
        }
    }

  return GDK_EVENT_STOP;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test various charts");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofSession) session = NULL;
  g_autoptr(SysprofXYSeries) samples_series = NULL;
  g_autoptr(SysprofXYSeries) num_series = NULL;
  g_autoptr(SysprofTimeSeries) marks_series = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GListModel) marks = NULL;
  g_autoptr(GError) error = NULL;
  const SysprofTimeSpan *time_span;
  GtkWidget *chart;
  GtkWidget *layer;
  GtkWidget *split;
  GtkWindow *window;
  GtkWidget *box;
  GdkRGBA blue = {0,0,1,1};
  GdkRGBA red = {1,0,0,1};
  guint n_samples;
  guint n_marks;

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

  loader = sysprof_document_loader_new (filename);

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  g_print ("loaded\n");

  session = sysprof_session_new (document);
  time_span = sysprof_document_get_time_span (document);
  marks = sysprof_document_list_marks (document);

  /* Generate an XY Series using the stacktraces depth for Y */
  samples = sysprof_document_list_samples (document);
  n_samples = g_list_model_get_n_items (samples);
  samples_series = sysprof_xy_series_new (samples, time_span->begin_nsec, 0, time_span->end_nsec, 128);
  for (guint i = 0; i < n_samples; i++)
    {
      g_autoptr(SysprofDocumentTraceable) sample = g_list_model_get_item (samples, i);
      gint64 time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (sample));
      guint depth = sysprof_document_traceable_get_stack_depth (sample);

      sysprof_xy_series_add (samples_series, time, depth, i);
    }
  sysprof_xy_series_sort (samples_series);

  num_series = sysprof_xy_series_new (NULL, 0, 0, 100, 100);
  for (guint i = 0; i < 100; i++)
    sysprof_xy_series_add (num_series, i, g_random_int_range (0, 100), 0);

  g_print ("series built\n");

  marks_series = sysprof_time_series_new (marks, *sysprof_document_get_time_span (document));
  n_marks = g_list_model_get_n_items (marks);
  for (guint i = 0; i < n_marks; i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (marks, i);
      gint64 time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      gint64 duration = sysprof_document_mark_get_duration (mark);

      sysprof_time_series_add (marks_series,
                               (SysprofTimeSpan) {time, time+duration},
                               i);
    }
  sysprof_time_series_sort (marks_series);

  window = g_object_new (GTK_TYPE_WINDOW,
                         "default-width", 800,
                         "default-height", 600,
                         NULL);
  g_signal_connect_swapped (window,
                            "close-request",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);

  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      NULL);
  gtk_window_set_child (window, GTK_WIDGET (box));

  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "session", session,
                        "title", "Samples",
                        "height-request", 128,
                        NULL);
  g_signal_connect (chart,
                    "activate-layer-item",
                    G_CALLBACK (activate_layer_item_cb),
                    document);
  layer = g_object_new (SYSPROF_TYPE_COLUMN_LAYER,
                        "series", samples_series,
                        "title", "Stack Depth",
                        NULL);
  sysprof_chart_add_layer (SYSPROF_CHART (chart),
                           SYSPROF_CHART_LAYER (layer));
  gtk_box_append (GTK_BOX (box), chart);

  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "session", session,
                        "height-request", 128,
                        NULL);
  split = g_object_new (SYSPROF_TYPE_SPLIT_LAYER,
                        "top", g_object_new (SYSPROF_TYPE_LINE_LAYER,
                                             "series", num_series,
                                             "title", "Stack Depth as Line",
                                             "fill", TRUE,
                                             NULL),
                        "bottom", g_object_new (SYSPROF_TYPE_LINE_LAYER,
                                                "series", num_series,
                                                "title", "Stack Depth as Line",
                                                "flip-y", TRUE,
                                                NULL),
                        NULL);
  sysprof_chart_add_layer (SYSPROF_CHART (chart),
                           SYSPROF_CHART_LAYER (split));
  gtk_box_append (GTK_BOX (box), chart);

  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "session", session,
                        "height-request", 128,
                        NULL);
  layer = g_object_new (SYSPROF_TYPE_LINE_LAYER,
                        "series", num_series,
                        "use-curves", TRUE,
                        "fill", TRUE,
                        "dashed", TRUE,
                        NULL),
  sysprof_chart_add_layer (SYSPROF_CHART (chart),
                           SYSPROF_CHART_LAYER (layer));
  gtk_box_append (GTK_BOX (box), chart);

  chart = g_object_new (SYSPROF_TYPE_CHART,
                        "session", session,
                        "height-request", 24,
                        NULL);
  layer = g_object_new (SYSPROF_TYPE_TIME_SPAN_LAYER,
                        "color", &blue,
                        "event-color", &red,
                        "series", marks_series,
                        NULL),
  sysprof_chart_add_layer (SYSPROF_CHART (chart),
                           SYSPROF_CHART_LAYER (layer));
  gtk_box_append (GTK_BOX (box), chart);

  gtk_window_present (window);
  g_main_loop_run (main_loop);

  return 0;
}
