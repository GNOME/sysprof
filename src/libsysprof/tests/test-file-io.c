/* test-file-io.c
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "sysprof-util-private.h"

static char *test_root;

static void
test_load_file (void)
{
  g_autofree char *path = g_build_filename (test_root, "value", NULL);
  g_autofree char *contents = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_true (g_file_set_contents (path, "12345\n", -1, &error));
  g_assert_no_error (error);
  contents = dex_await_string (_sysprof_load_file (path), &error);
  g_assert_no_error (error);
  g_assert_cmpstr (contents, ==, "12345\n");
  g_clear_pointer (&contents, g_free);

  g_assert_true (g_file_set_contents (path, "", 0, &error));
  g_assert_no_error (error);
  contents = dex_await_string (_sysprof_load_file (path), &error);
  g_assert_no_error (error);
  g_assert_cmpstr (contents, ==, "");
  g_clear_pointer (&contents, g_free);

  g_assert_cmpint (g_unlink (path), ==, 0);
  contents = dex_await_string (_sysprof_load_file (path), &error);
  g_assert_null (contents);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_list_directory (void)
{
  g_autofree char *directory = g_build_filename (test_root, "device", NULL);
  g_autofree char *alias = g_build_filename (test_root, "alias", NULL);
  g_autofree char *identity = NULL;
  g_autoptr(GPtrArray) children = NULL;
  g_autoptr(GError) error = NULL;

  g_assert_cmpint (g_mkdir (directory, 0700), ==, 0);
  g_assert_cmpint (symlink ("device", alias), ==, 0);

  children = dex_await_boxed (_sysprof_list_directory (test_root), &error);
  g_assert_no_error (error);
  g_assert_nonnull (children);
  g_assert_cmpuint (children->len, ==, 2);

  for (guint i = 0; i < children->len; i++)
    {
      GFileInfo *info = g_ptr_array_index (children, i);
      const char *id = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILE);

      g_assert_cmpint (g_file_info_get_file_type (info), ==, G_FILE_TYPE_DIRECTORY);
      g_assert_nonnull (id);
      if (identity == NULL)
        identity = g_strdup (id);
      else
        g_assert_cmpstr (identity, ==, id);
    }

  g_clear_pointer (&children, g_ptr_array_unref);
  children = dex_await_boxed (_sysprof_list_directory (directory), &error);
  g_assert_no_error (error);
  g_assert_nonnull (children);
  g_assert_cmpuint (children->len, ==, 0);
  g_clear_pointer (&children, g_ptr_array_unref);

  g_assert_cmpint (g_unlink (alias), ==, 0);
  g_assert_cmpint (g_rmdir (directory), ==, 0);
  children = dex_await_boxed (_sysprof_list_directory (directory), &error);
  g_assert_null (children);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static gpointer
write_fifo_thread (gpointer data)
{
  const char *path = data;
  g_autofd int fd = -1;

  g_usleep (200000);
  fd = open (path, O_WRONLY | O_CLOEXEC);
  g_assert_cmpint (fd, >=, 0);
  g_assert_cmpint (write (fd, "ready", 5), ==, 5);

  return NULL;
}

static gboolean
tick_cb (gpointer data)
{
  gboolean *ticked = data;

  *ticked = TRUE;

  return G_SOURCE_REMOVE;
}

static void
test_load_file_yields (void)
{
  g_autofree char *path = g_build_filename (test_root, "fifo", NULL);
  g_autofree char *contents = NULL;
  g_autoptr(GError) error = NULL;
  GThread *writer;
  gboolean ticked = FALSE;

  g_assert_cmpint (mkfifo (path, 0600), ==, 0);
  writer = g_thread_new ("fifo-writer", write_fifo_thread, path);
  g_timeout_add (10, tick_cb, &ticked);

  contents = dex_await_string (_sysprof_load_file (path), &error);
  g_assert_no_error (error);
  g_assert_cmpstr (contents, ==, "ready");
  g_assert_true (ticked);

  g_thread_join (writer);
  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
test_online_cpus (void)
{
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GError) error = NULL;

  cpus = dex_await_boxed (_sysprof_list_online_cpus (), &error);
  g_assert_no_error (error);
  g_assert_nonnull (cpus);
  g_assert_cmpuint (cpus->len, >, 0);

  for (guint i = 1; i < cpus->len; i++)
    g_assert_cmpuint (g_array_index (cpus, guint, i - 1), <, g_array_index (cpus, guint, i));
}

int
main (int   argc,
      char *argv[])
{
  int ret;

  g_test_init (&argc, &argv, NULL);
  dex_init ();

  test_root = g_dir_make_tmp ("sysprof-file-io-XXXXXX", NULL);
  g_assert_nonnull (test_root);

  dex_test_add_func ("/file-io/load", test_load_file);
  dex_test_add_func ("/file-io/list", test_list_directory);
  dex_test_add_func ("/file-io/load-yields", test_load_file_yields);
  dex_test_add_func ("/file-io/online-cpus", test_online_cpus);

  ret = g_test_run ();

  g_assert_cmpint (g_rmdir (test_root), ==, 0);
  g_free (test_root);

  return ret;
}
