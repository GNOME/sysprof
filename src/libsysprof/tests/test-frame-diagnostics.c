/* test-frame-diagnostics.c
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

#include <string.h>

#include <glib/gstdio.h>
#include <sysprof.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static SysprofCaptureWriter *
new_capture (char **path)
{
  SysprofCaptureWriter *writer;
  g_autoptr(GError) error = NULL;
  int fd = g_file_open_tmp ("sysprof-frame-XXXXXX", path, &error);

  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);

  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  g_assert_nonnull (writer);

  return writer;
}

static SysprofDocument *
load_capture (SysprofCaptureWriter *writer,
              const char           *path)
{
  g_autoptr(SysprofDocumentLoader) loader = sysprof_document_loader_new (path);
  g_autoptr(GError) error = NULL;
  SysprofDocument *document;

  g_assert_true (sysprof_capture_writer_flush (writer));

  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);
  g_assert_cmpint (g_unlink (path), ==, 0);

  return document;
}

static void
add_compositor_frame (SysprofCaptureWriter *writer,
                      guint                 frame,
                      gint64                begin,
                      gint64                target,
                      gint64                presentation)
{
  g_autofree char *dispatch = NULL;
  g_autofree char *presented = NULL;

  dispatch = g_strdup_printf ("eDP-1, clock=1 frame=%u target_us=%" G_GINT64_FORMAT
                              " prediction_us=%" G_GINT64_FORMAT
                              " submit_deadline_us=0 refresh_interval_us=16667 mode=fixed",
                              frame,
                              target / 1000,
                              (target ? target : presentation - 5000000) / 1000);
  presented = g_strdup_printf ("eDP-1, presentation was 100 µs earlier, "
                               "clock=1 frame=%u actual_us=%" G_GINT64_FORMAT " flags=0",
                               frame,
                               presentation / 1000);

  g_assert_true (sysprof_capture_writer_add_mark (writer, begin, -1, 42, 100000,
                                                  "Dispatch group", "Clutter::FrameClock::dispatch()",
                                                  dispatch));
  g_assert_true (sysprof_capture_writer_add_mark (writer, presentation + 100000, -1, 42,
                                                  100000,
                                                  "Presentation group", "Clutter::FrameClock::presented()",
                                                  presented));
}

static void
test_missed_presentation (void)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) diagnostics = NULL;
  g_autoptr(GListModel) evidence = NULL;
  g_autoptr(SysprofDiagnostic) diagnostic = NULL;
  g_autoptr(SysprofDocumentMark) dispatch_mark = NULL;
  g_autoptr(SysprofDocumentMark) presentation_mark = NULL;
  g_autofree char *path = NULL;
  g_autofree char *legacy_dispatch = NULL;
  g_autofree char *incomplete_dispatch = NULL;
  gint64 base = (SYSPROF_CAPTURE_CURRENT_TIME / 1000) * 1000;
  const SysprofTimeSpan *span;

  writer = new_capture (&path);

  g_assert_true (sysprof_capture_writer_add_mark (writer, base, -1, 73, 18000000,
                                                  "GTK", "Frameclock cycle",
                                                  "clock=0x123 frame=1"));

  add_compositor_frame (writer,
                        1,
                        base + 20000000,
                        base + 30000000,
                        base + 30000000);

  add_compositor_frame (writer,
                        2,
                        base + 40000000,
                        base + 50000000,
                        base + 55000000);

  add_compositor_frame (writer,
                        3,
                        base + 60000000,
                        0,
                        base + 90000000);
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 41000000, -1, 42,
                                                  100000, "Other group", "Unrelated mark",
                                                  "eDP-1, clock=1 frame=2"));

  legacy_dispatch = g_strdup_printf ("eDP-1, clock=1 frame=4 target_us=%" G_GINT64_FORMAT,
                                     (base + 120000000) / 1000);
  incomplete_dispatch = g_strdup_printf ("eDP-1, clock=1 frame=5 target_us=%" G_GINT64_FORMAT,
                                         (base + 150000000) / 1000);

  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 100000000,
                                                  -1, 42, 100000,
                                                  "Mutter", "Clutter::FrameClock::dispatch()",
                                                  legacy_dispatch));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 130000000,
                                                  -1, 42, 100000,
                                                  "Mutter", "Clutter::FrameClock::presented()",
                                                  "eDP-1, presentation was 0 µs earlier"));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 140000000,
                                                  -1, 42, 100000,
                                                  "Mutter", "Clutter::FrameClock::dispatch()",
                                                  incomplete_dispatch));

  document = load_capture (writer, path);

  diagnostics = sysprof_document_list_diagnostics (document);
  g_assert_cmpuint (g_list_model_get_n_items (diagnostics), ==, 1);

  diagnostic = g_list_model_get_item (diagnostics, 0);
  g_assert_false (sysprof_diagnostic_get_fatal (diagnostic));
  g_assert_cmpstr (sysprof_diagnostic_get_domain (diagnostic), ==, "Mutter");
  g_assert_nonnull (strstr (sysprof_diagnostic_get_message (diagnostic), "5.000 ms"));

  span = sysprof_diagnostic_get_time_span (diagnostic);
  g_assert_nonnull (span);
  g_assert_cmpint (span->begin_nsec, ==, base + 40000000);
  g_assert_cmpint (span->end_nsec, ==, base + 55000000);

  evidence = sysprof_diagnostic_list_frames (diagnostic);
  g_assert_cmpuint (g_list_model_get_n_items (evidence), ==, 2);

  dispatch_mark = g_list_model_get_item (evidence, 0);
  presentation_mark = g_list_model_get_item (evidence, 1);
  g_assert_cmpstr (sysprof_document_mark_get_name (dispatch_mark), ==,
                   "Clutter::FrameClock::dispatch()");
  g_assert_cmpstr (sysprof_document_mark_get_name (presentation_mark), ==,
                   "Clutter::FrameClock::presented()");
}

static void
test_equal_time_candidates (void)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofFrameCycle) cycle = NULL;
  g_autofree char *path = NULL;
  gint64 base = (SYSPROF_CAPTURE_CURRENT_TIME / 1000) * 1000;

  writer = new_capture (&path);

  g_assert_true (sysprof_capture_writer_add_mark (writer, base, -1, 42, 100000,
                                                  "First group", "Clutter::FrameClock::dispatch()",
                                                  "eDP-1, clock=1 frame=1"));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base, -1, 42, 100000,
                                                  "Second group", "Clutter::FrameClock::dispatch()",
                                                  "eDP-1, clock=1 frame=2"));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 200000, -1, 42, 100000,
                                                  "Other group", "Unrelated mark",
                                                  "eDP-1, clock=1 frame=1"));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 400000, -1, 42, 100000,
                                                  "Third group", "Clutter::FrameClock::presented()",
                                                  "eDP-1, clock=1 frame=2"));
  g_assert_true (sysprof_capture_writer_add_mark (writer, base + 600000, -1, 42, 100000,
                                                  "Fourth group", "Clutter::FrameClock::presented()",
                                                  "eDP-1, clock=1 frame=1"));

  document = load_capture (writer, path);

  cycle = sysprof_frame_cycle_new (document);
  g_assert_cmpuint (sysprof_frame_cycle_get_n_frames (cycle), ==, 2);

  for (guint i = 0; i < 2; i++)
    {
      const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, i);

      g_assert_cmpint (frame->result, ==, SYSPROF_FRAME_CYCLE_PRESENTED);
      g_assert_nonnull (frame->dispatch_mark);
      g_assert_nonnull (frame->presentation_mark);
    }

}

static void
test_diagnostic_limit (void)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) diagnostics = NULL;
  g_autofree char *path = NULL;
  gint64 base = (SYSPROF_CAPTURE_CURRENT_TIME / 1000) * 1000;

  writer = new_capture (&path);

  for (guint i = 0; i < 1004; i++)
    {
      gint64 begin = base + i * 20000000;

      add_compositor_frame (writer,
                            i + 1,
                            begin,
                            begin + 8000000,
                            begin + 12000000);
    }

  document = load_capture (writer, path);
  diagnostics = sysprof_document_list_diagnostics (document);
  g_assert_cmpuint (g_list_model_get_n_items (diagnostics), ==, 1000);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  sysprof_clock_init ();

  g_test_add_func ("/document/frame-diagnostics/missed", test_missed_presentation);
  g_test_add_func ("/document/frame-diagnostics/equal-time", test_equal_time_candidates);
  g_test_add_func ("/document/frame-diagnostics/limit", test_diagnostic_limit);

  return g_test_run ();
}
