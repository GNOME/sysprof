/* sysprof-mark-preparation.c
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

#include <glib/gstdio.h>
#include <libdex.h>
#include "sysprof-marks-section-model.h"
#include "sysprof-marks-section-model-item.h"
#include "sysprof-mark-preparation-private.h"
#include "sysprof-mark-table.h"
#include "sysprof-time-filter-model.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static SysprofDocument *
new_document (guint count)
{
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  SysprofDocument *document;
  int fd = g_file_open_tmp ("sysprof-marks-XXXXXX", &path, &error);
  gint64 base;

  g_assert_no_error (error);
  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  base = SYSPROF_CAPTURE_CURRENT_TIME;
  for (guint i = 0; i < count; i++)
    g_assert_true (sysprof_capture_writer_add_mark (writer, base + i / 2, -1, 42,
                                                   (i * 13) % 1000,
                                                   i % 2 ? "A" : "B",
                                                   i % 3 ? "same" : "other", "message"));
  g_assert_true (sysprof_capture_writer_flush (writer));
  loader = sysprof_document_loader_new (path);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_unlink (path);
  return document;
}

static void
wait_ready (SysprofMarksSectionModel *model)
{
  gint64 deadline = g_get_monotonic_time () + 30 * G_USEC_PER_SEC;
  gboolean loading = TRUE;
  gboolean error = FALSE;

  while (loading)
    {
      g_main_context_iteration (NULL, TRUE);
      g_object_get (model,
                    "loading", &loading,
                    "error", &error,
                    NULL);
      g_assert_false (error);
      g_assert_cmpint (g_get_monotonic_time (), <, deadline);
    }
}

static void
assert_order (GListModel *model)
{
  gint64 last = G_MININT64;

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentFrame) frame = g_list_model_get_item (model, i);
      gint64 time = sysprof_document_frame_get_time (frame);

      g_assert_cmpint (time, >=, last);
      last = time;
    }
}

static void
assert_catalogs (SysprofDocument *document,
                 GListModel      *actual)
{
  g_autoptr(GListModel) expected = sysprof_document_catalog_marks (document);
  guint matched = 0;

  for (guint i = 0; i < g_list_model_get_n_items (expected); i++)
    {
      g_autoptr(GListModel) group = g_list_model_get_item (expected, i);

      for (guint j = 0; j < g_list_model_get_n_items (group); j++)
        {
          g_autoptr(SysprofMarkCatalog) a = g_list_model_get_item (group, j);

          for (guint k = 0; k < g_list_model_get_n_items (actual); k++)
            {
              g_autoptr(SysprofMarksSectionModelItem) item = g_list_model_get_item (actual, k);
              SysprofMarkCatalog *b = sysprof_marks_section_model_item_get_item (item);

              if (g_str_equal (sysprof_mark_catalog_get_group (a), sysprof_mark_catalog_get_group (b)) &&
                  g_str_equal (sysprof_mark_catalog_get_name (a), sysprof_mark_catalog_get_name (b)))
                {
                  g_assert_cmpint (sysprof_mark_catalog_get_min_duration (a), ==, sysprof_mark_catalog_get_min_duration (b));
                  g_assert_cmpint (sysprof_mark_catalog_get_max_duration (a), ==, sysprof_mark_catalog_get_max_duration (b));
                  g_assert_cmpint (sysprof_mark_catalog_get_average_duration (a), ==, sysprof_mark_catalog_get_average_duration (b));
                  g_assert_cmpint (sysprof_mark_catalog_get_median_duration (a), ==, sysprof_mark_catalog_get_median_duration (b));
                  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (a)), ==, g_list_model_get_n_items (G_LIST_MODEL (b)));
                  matched++;
                }
            }
        }
    }

  g_assert_cmpuint (matched, ==, g_list_model_get_n_items (actual));
}

static void
items_changed (GListModel *model,
               guint       position,
               guint       removed,
               guint       added,
               guint      *count)
{
  (*count)++;
}

static void
test_marks (void)
{
  g_autoptr(SysprofDocument) document = new_document (20000);
  g_autoptr(SysprofSession) session = sysprof_session_new (document);
  g_autoptr(SysprofMarksSectionModel) model = g_object_new (SYSPROF_TYPE_MARKS_SECTION_MODEL, NULL);
  GListModel *catalog;
  guint notifications = 0;

  g_signal_connect (model, "items-changed", G_CALLBACK (items_changed), &notifications);
  sysprof_marks_section_model_set_session (model, session);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);
  wait_ready (model);
  g_assert_cmpuint (notifications, ==, 1);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 20000);
  assert_order (G_LIST_MODEL (model));
  {
    g_autoptr(GListModel) marks = sysprof_document_list_marks (document);
    g_autoptr(SysprofTimeFilterModel) filtered = NULL;
    SysprofTimeSpan span = *sysprof_session_get_document_time (session);

    for (guint i = 0; i < g_list_model_get_n_items (marks); i++)
      {
        g_autoptr(SysprofDocumentMark) a = g_list_model_get_item (marks, i);
        g_autoptr(SysprofDocumentMark) b = g_list_model_get_item (G_LIST_MODEL (model), i);

        g_assert_cmpstr (sysprof_document_mark_get_group (a), ==, sysprof_document_mark_get_group (b));
        g_assert_cmpint (sysprof_document_mark_get_duration (a), ==, sysprof_document_mark_get_duration (b));
      }

    {
      g_autoptr(SysprofDocumentFrame) first = g_list_model_get_item (marks, 0);

      span.begin_nsec = sysprof_document_frame_get_time (first);
      span.end_nsec = span.begin_nsec + 100;
    }
    filtered = sysprof_time_filter_model_new (g_object_ref (G_LIST_MODEL (model)), &span);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (filtered)), >, 0);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (filtered)), <, 20000);
    assert_order (G_LIST_MODEL (filtered));
  }
  catalog = sysprof_marks_section_model_get_mark_catalog (model);
  assert_catalogs (document, catalog);

  for (guint i = 0; i < g_list_model_get_n_items (catalog); i++)
    {
      g_autoptr(SysprofMarksSectionModelItem) item = g_list_model_get_item (catalog, i);

      sysprof_marks_section_model_item_set_visible (item, FALSE);
    }
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 20000);
  wait_ready (model);
  g_assert_cmpuint (notifications, ==, 2);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);

  for (guint i = 0; i < g_list_model_get_n_items (catalog); i++)
    {
      g_autoptr(SysprofMarksSectionModelItem) item = g_list_model_get_item (catalog, i);
      SysprofMarkCatalog *entry = sysprof_marks_section_model_item_get_item (item);

      sysprof_marks_section_model_item_set_visible (item, TRUE);
      g_main_context_iteration (NULL, FALSE);
      sysprof_marks_section_model_item_set_visible (item,
                                                    g_str_equal (sysprof_mark_catalog_get_group (entry), "A") &&
                                                    g_str_equal (sysprof_mark_catalog_get_name (entry), "same"));
    }
  wait_ready (model);
  assert_order (G_LIST_MODEL (model));
  for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (model)); i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (G_LIST_MODEL (model), i);

      g_assert_cmpstr (sysprof_document_mark_get_group (mark), ==, "A");
      g_assert_cmpstr (sysprof_document_mark_get_name (mark), ==, "same");
    }
  g_assert_cmpuint (g_list_model_get_n_items (sysprof_marks_section_model_get_marks (model)), ==, 20000);

  for (guint i = 0; i < 20; i++)
    {
      sysprof_marks_section_model_set_session (model, NULL);
      sysprof_marks_section_model_set_session (model, session);
      g_main_context_iteration (NULL, FALSE);
    }
  wait_ready (model);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 20000);
  sysprof_marks_section_model_set_session (model, NULL);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);
  {
    g_autoptr(SysprofDocument) empty = new_document (0);
    g_autoptr(SysprofSession) replacement = sysprof_session_new (empty);

    sysprof_marks_section_model_set_session (model, session);
    sysprof_marks_section_model_set_session (model, replacement);
    wait_ready (model);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);
    sysprof_marks_section_model_set_session (model, session);
  }
  g_clear_object (&model);
  for (guint i = 0; i < 100; i++)
    g_main_context_iteration (NULL, FALSE);
}

static void
test_empty (void)
{
  g_autoptr(SysprofDocument) document = new_document (0);
  g_autoptr(SysprofSession) session = sysprof_session_new (document);
  g_autoptr(SysprofMarksSectionModel) model = g_object_new (SYSPROF_TYPE_MARKS_SECTION_MODEL,
                                                            "session", session,
                                                            NULL);

  wait_ready (model);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);
}

static void
test_table (void)
{
  g_autoptr(SysprofDocument) document = new_document (20000);
  g_autoptr(SysprofSession) session = sysprof_session_new (document);
  GtkWidget *table;
  GtkWindow *window;
  GtkColumnView *view;
  GtkSortListModel *sort;
  SysprofTimeFilterModel *filter;

  if (!gtk_init_check ())
    {
      g_test_skip ("No display available");
      return;
    }

  table = sysprof_mark_table_new ();
  window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_child (window, table);
  view = GTK_COLUMN_VIEW (gtk_widget_get_template_child (table, SYSPROF_TYPE_MARK_TABLE, "column_view"));
  sort = GTK_SORT_LIST_MODEL (gtk_widget_get_template_child (table,
                                                             SYSPROF_TYPE_MARK_TABLE,
                                                             "sort_model"));
  filter = SYSPROF_TIME_FILTER_MODEL (gtk_widget_get_template_child (table,
                                                                     SYSPROF_TYPE_MARK_TABLE,
                                                                     "time_filter"));
  sysprof_mark_table_set_session (SYSPROF_MARK_TABLE (table), session);
  g_assert_null (sysprof_time_filter_model_get_model (filter));
  g_assert_true (gtk_sort_list_model_get_incremental (sort));
  gtk_window_present (window);
  while (!gtk_widget_get_mapped (table) || gtk_sort_list_model_get_pending (sort))
    g_main_context_iteration (NULL, TRUE);
  g_assert_nonnull (sysprof_time_filter_model_get_model (filter));
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (sort)), ==, 20000);
  assert_order (G_LIST_MODEL (sort));
  {
    g_autoptr(GtkColumnViewColumn) column = g_list_model_get_item (gtk_column_view_get_columns (view), 1);
    gint64 previous = G_MAXINT64;

    gtk_column_view_sort_by_column (view, column, GTK_SORT_ASCENDING);
    while (gtk_sort_list_model_get_pending (sort))
      g_main_context_iteration (NULL, TRUE);
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (sort)); i++)
      {
        g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (G_LIST_MODEL (sort), i);
        gint64 duration = sysprof_document_mark_get_duration (mark);

        g_assert_cmpint (duration, <=, previous);
        previous = duration;
      }
  }
  gtk_widget_set_visible (table, FALSE);
  g_assert_nonnull (sysprof_time_filter_model_get_model (filter));
  gtk_widget_set_visible (table, TRUE);
  g_signal_emit_by_name (view, "activate", 1);
  sysprof_mark_table_set_session (SYSPROF_MARK_TABLE (table), NULL);
  g_assert_null (sysprof_time_filter_model_get_model (filter));
  sysprof_mark_table_set_session (SYSPROF_MARK_TABLE (table), session);
  gtk_window_destroy (window);
}

static void
benchmark_capture (const char *path)
{
  g_autoptr(SysprofDocumentLoader) loader = sysprof_document_loader_new (path);
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofSession) session = NULL;
  g_autoptr(SysprofMarksSectionModel) model = NULL;
  g_autoptr(GError) error = NULL;
  gint64 start = g_get_monotonic_time ();
  gint64 phase;
  gint64 max_slice = 0;
  gboolean loading = TRUE;
  GtkWidget *table;
  GtkWindow *window;
  GtkSortListModel *sort;
  GListModel *catalog;

  gtk_init ();
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_print ("Document load: %.3f ms\n", (g_get_monotonic_time () - start) / 1000.0);
  session = sysprof_session_new (document);
  phase = g_get_monotonic_time ();
  model = g_object_new (SYSPROF_TYPE_MARKS_SECTION_MODEL,
                        "session", session,
                        NULL);
  g_print ("Initial model attachment: %.3f ms\n", (g_get_monotonic_time () - phase) / 1000.0);
  while (loading)
    {
      gint64 slice = g_get_monotonic_time ();

      g_main_context_iteration (NULL, FALSE);
      max_slice = MAX (max_slice, g_get_monotonic_time () - slice);
      g_object_get (model,
                    "loading", &loading,
                    NULL);
    }
  g_print ("Marks ready: %.3f ms; max main dispatch: %.3f ms; %u marks\n",
           (g_get_monotonic_time () - phase) / 1000.0,
           max_slice / 1000.0,
           g_list_model_get_n_items (G_LIST_MODEL (model)));

  catalog = sysprof_marks_section_model_get_mark_catalog (model);
  phase = g_get_monotonic_time ();
  for (guint i = 0; i < g_list_model_get_n_items (catalog); i++)
    {
      g_autoptr(SysprofMarksSectionModelItem) item = g_list_model_get_item (catalog, i);

      sysprof_marks_section_model_item_set_visible (item, i % 2 == 0);
    }
  wait_ready (model);
  g_print ("Filter update: %.3f ms\n", (g_get_monotonic_time () - phase) / 1000.0);

  table = sysprof_mark_table_new ();
  window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_child (window, table);
  sysprof_mark_table_set_session (SYSPROF_MARK_TABLE (table), session);
  sort = GTK_SORT_LIST_MODEL (gtk_widget_get_template_child (table,
                                                             SYSPROF_TYPE_MARK_TABLE,
                                                             "sort_model"));
  phase = g_get_monotonic_time ();
  gtk_window_present (window);
  g_print ("First table activation: %.3f ms\n", (g_get_monotonic_time () - phase) / 1000.0);
  max_slice = 0;
  while (!gtk_widget_get_mapped (table) || gtk_sort_list_model_get_pending (sort))
    {
      gint64 slice = g_get_monotonic_time ();

      g_main_context_iteration (NULL, FALSE);
      max_slice = MAX (max_slice, g_get_monotonic_time () - slice);
    }
  g_print ("Table sorted: %.3f ms; max main dispatch: %.3f ms; total: %.3f ms\n",
           (g_get_monotonic_time () - phase) / 1000.0,
           max_slice / 1000.0,
           (g_get_monotonic_time () - start) / 1000.0);
  assert_order (G_LIST_MODEL (sort));
  gtk_window_destroy (window);
}

int
main (int    argc,
      char **argv)
{
  dex_init ();
  if (g_getenv ("SYSPROF_TEST_CAPTURE"))
    {
      benchmark_capture (g_getenv ("SYSPROF_TEST_CAPTURE"));
      return 0;
    }

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/marks/catalog-filter-lifecycle", test_marks);
  g_test_add_func ("/marks/empty", test_empty);
  g_test_add_func ("/marks/table", test_table);
  return g_test_run ();
}
