/* test-process-filter.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include "sysprof-process-filter-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static void
test_process_lifetimes (void)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) processes = NULL;
  g_autoptr(GtkFilterListModel) model = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  gint64 base = SYSPROF_CAPTURE_CURRENT_TIME;
  int fd;
  const struct {
    gint64 begin;
    gint64 end;
    guint pids;
  } cases[] = {
    { 100, 300, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5) },
    { 160, 190, (1 << 2) },
    { 200, 250, (1 << 2) | (1 << 3) },
    { 150, 200, (1 << 1) | (1 << 2) | (1 << 3) },
    { 260, 280, (1 << 2) | (1 << 5) },
    { 301, 310, 0 },
    { 200, 100, 0 },
  };

  fd = g_file_open_tmp ("sysprof-process-filter-XXXXXX", &path, &error);
  g_assert_no_error (error);
  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  g_assert_nonnull (writer);

  /* Existing processes share a snapshot timestamp before recording begins. */
  g_assert_true (sysprof_capture_writer_add_process (writer, base + 90, -1, 1, "existing"));
  g_assert_true (sysprof_capture_writer_add_process (writer, base + 90, -1, 2, "long-running"));
  g_assert_true (sysprof_capture_writer_add_process (writer, base + 90, -1, 4, "already-exited"));
  g_assert_true (sysprof_capture_writer_add_exit (writer, base + 95, -1, 4));
  g_assert_true (sysprof_capture_writer_add_exit (writer, base + 150, -1, 1));
  g_assert_true (sysprof_capture_writer_add_process (writer, base + 200, -1, 3, "spawned"));
  g_assert_true (sysprof_capture_writer_add_exit (writer, base + 250, -1, 3));
  g_assert_true (sysprof_capture_writer_add_process (writer, base + 275, -1, 5, "late"));
  g_assert_true (sysprof_capture_writer_add_timestamp (writer, base + 300, -1, -1));
  g_assert_true (sysprof_capture_writer_flush (writer));
  g_assert_true (_sysprof_capture_writer_set_time_range (writer, base + 100, base + 300));

  loader = sysprof_document_loader_new (path);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);
  processes = sysprof_document_list_processes (document);
  g_assert_cmpuint (g_list_model_get_n_items (processes), ==, 5);
  model = gtk_filter_list_model_new (g_object_ref (processes), NULL);

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      SysprofTimeSpan span = { base + cases[i].begin, base + cases[i].end };
      g_autoptr(GtkFilter) filter = NULL;
      guint pids = 0;

      filter = _sysprof_process_filter_new (&span);
      gtk_filter_list_model_set_filter (model, filter);

      for (guint j = 0; j < g_list_model_get_n_items (G_LIST_MODEL (model)); j++)
        {
          g_autoptr(SysprofDocumentFrame) process = NULL;

          process = g_list_model_get_item (G_LIST_MODEL (model), j);
          pids |= 1 << sysprof_document_frame_get_pid (process);
        }

      g_assert_cmpuint (pids, ==, cases[i].pids);
    }

  {
    g_autoptr(GtkFilter) filter = NULL;

    filter = _sysprof_process_filter_new (NULL);
    gtk_filter_list_model_set_filter (model, filter);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 5);
  }

  g_assert_cmpint (g_unlink (path), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  sysprof_clock_init ();
  g_test_add_func ("/process-filter/lifetimes", test_process_lifetimes);
  return g_test_run ();
}
