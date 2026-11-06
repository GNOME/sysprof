/* test-callgraph-build.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>
#include <unistd.h>

#include <sysprof.h>

#include "sysprof-callgraph-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static SysprofDocument *
create_document (void)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofSymbolizer) symbolizer = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  SysprofCaptureAddress addresses[3];
  gint64 time = SYSPROF_CAPTURE_CURRENT_TIME;
  int fd;

  fd = g_file_open_tmp ("sysprof-callgraph-XXXXXX", &filename, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_cmpint (unlink (filename), ==, 0);

  writer = sysprof_capture_writer_new_from_fd (dup (fd), 0);
  g_assert_nonnull (writer);
  g_assert_true (sysprof_capture_writer_add_process (writer, time, 0, 123, "test"));
  addresses[0] = sysprof_capture_writer_add_jitmap (writer, "charlie");
  addresses[1] = sysprof_capture_writer_add_jitmap (writer, "alpha");
  addresses[2] = sysprof_capture_writer_add_jitmap (writer, "bravo");

  for (guint i = 0; i < G_N_ELEMENTS (addresses); i++)
    for (guint j = 0; j <= i; j++)
      g_assert_true (sysprof_capture_writer_add_sample (writer, time++, 0, 123, 123,
                                                        &addresses[i], 1));

  g_assert_true (sysprof_capture_writer_flush (writer));
  loader = sysprof_document_loader_new_for_fd (fd, &error);
  g_assert_no_error (error);
  close (fd);

  symbolizer = sysprof_jitmap_symbolizer_new ();
  sysprof_document_loader_set_symbolizer (loader, symbolizer);
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);

  return g_steal_pointer (&document);
}

static void
callgraph_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  GAsyncResult **location = user_data;

  *location = g_object_ref (result);
}

static SysprofCallgraph *
build_callgraph (SysprofDocument          *document,
                 SysprofCallgraphFlags     flags,
                 SysprofAugmentationFunc   augment,
                 gpointer                  augment_data,
                 GCancellable             *cancellable,
                 GError                  **error)
{
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GAsyncResult) result = NULL;

  samples = sysprof_document_list_samples (document);
  _sysprof_callgraph_new_async (document, flags, samples, 128, augment,
                                augment_data, NULL, cancellable,
                                callgraph_cb, &result);
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  return _sysprof_callgraph_new_finish (result, error);
}

static void
test_sort (void)
{
  g_autoptr(SysprofDocument) document = NULL;
  const char *names[][3] = {
    { "alpha", "bravo", "charlie" },
    { "bravo", "alpha", "charlie" },
  };

  document = create_document ();

  for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
      g_autoptr(SysprofCallgraph) callgraph = NULL;
      g_autoptr(GError) error = NULL;
      SysprofCallgraphNode *node;
      SysprofCallgraphNode *prev = NULL;
      SysprofCallgraphFlags flags = i == 0 ? 0 : SYSPROF_CALLGRAPH_FLAGS_LEFT_HEAVY;

      callgraph = build_callgraph (document, flags, NULL, NULL, NULL, &error);
      g_assert_no_error (error);
      g_assert_nonnull (callgraph);
      node = callgraph->root.children;
      g_assert_nonnull (node);
      g_assert_null (node->next);
      node = node->children;

      for (guint j = 0; j < G_N_ELEMENTS (names[i]); j++)
        {
          g_assert_nonnull (node);
          g_assert_true (node->prev == prev);
          g_assert_cmpstr (sysprof_symbol_get_name (node->summary->symbol), ==, names[i][j]);
          prev = node;
          node = node->next;
        }

      g_assert_null (node);
    }
}

typedef struct
{
  GCancellable *cancellable;
  guint         count;
} CancelData;

static void
cancel_augment (SysprofCallgraph     *callgraph,
                SysprofCallgraphNode *node,
                SysprofDocumentFrame *frame,
                gboolean              summarize,
                gpointer              user_data)
{
  CancelData *data = user_data;

  data->count++;
  g_assert_nonnull (sysprof_callgraph_get_augment (callgraph, node));
  g_cancellable_cancel (data->cancellable);
}

static void
test_cancel (void)
{
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(GError) error = NULL;
  CancelData data = { 0 };

  document = create_document ();
  cancellable = g_cancellable_new ();
  data.cancellable = cancellable;
  callgraph = build_callgraph (document, 0, cancel_augment, &data, cancellable, &error);
  g_assert_null (callgraph);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (data.count, ==, 1);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/callgraph/sort", test_sort);
  g_test_add_func ("/callgraph/cancel", test_cancel);
  return g_test_run ();
}
