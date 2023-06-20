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

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test various charts");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofSession) session = NULL;
  g_autoptr(SysprofXYSeries) samples_series = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GError) error = NULL;
  const SysprofTimeSpan *time_span;
  GtkWidget *chart;
  GtkWidget *layer;
  GtkWindow *window;
  GtkWidget *box;
  guint n_samples;

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
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  session = sysprof_session_new (document);
  time_span = sysprof_document_get_time_span (document);

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
                        "title", "Stack Traces",
                        "height-request", 128,
                        NULL);
  layer = g_object_new (SYSPROF_TYPE_DEPTH_LAYER,
                        "series", samples_series,
                        NULL);
  sysprof_chart_add_layer (SYSPROF_CHART (chart),
                           SYSPROF_CHART_LAYER (layer));
  gtk_box_append (GTK_BOX (box), chart);

  gtk_window_present (window);
  g_main_loop_run (main_loop);

  return 0;
}
