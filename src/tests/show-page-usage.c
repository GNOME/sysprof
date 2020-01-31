/* show-page-usage.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#include <cairo.h>
#include <stddef.h>
#include <rax.h>
#include <sysprof.h>

static GMainLoop *main_loop;

static gint
u64_compare (gconstpointer a,
             gconstpointer b)
{
  const guint64 *aptr = a;
  const guint64 *bptr = b;

  if (*aptr < *bptr)
    return -1;
  else if (*aptr > *bptr)
    return 1;
  else
    return 0;
}

static void
generate_cb (GObject      *object,
             GAsyncResult *result,
             gpointer      user_data)
{
  SysprofProfile *profile = (SysprofProfile *)object;
  g_autoptr(GError) error = NULL;
  GHashTable *seen;
  GHashTableIter iter;
  cairo_t *cr;
  cairo_surface_t *surface;
  GArray *ar;
  raxIterator it;
  rax *r;
  gpointer k,v;

  g_assert (SYSPROF_IS_MEMPROF_PROFILE (profile));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!sysprof_profile_generate_finish (profile, result, &error))
    {
      g_printerr ("%s\n", error->message);
      exit (EXIT_FAILURE);
    }

  r = sysprof_memprof_profile_get_native (SYSPROF_MEMPROF_PROFILE (profile));
  seen = g_hash_table_new (NULL, NULL);

  raxStart (&it, r);
  raxSeek (&it, "^", NULL, 0);
  while (raxNext (&it))
    {
      guint64 page;
      guint64 addr;

      memcpy (&addr, it.key, sizeof addr);
      page = addr / 4096;

      if (g_hash_table_contains (seen, GSIZE_TO_POINTER (page)))
        continue;

      g_hash_table_insert (seen, GSIZE_TO_POINTER (page), NULL);
    }
  raxStop (&it);

  ar = g_array_sized_new (FALSE, FALSE, sizeof (guint64), g_hash_table_size (seen));

  g_hash_table_iter_init (&iter, seen);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      guint64 key = GPOINTER_TO_SIZE (k);

      g_array_append_val (ar, key);
    }

  g_array_sort (ar, u64_compare);

  for (guint i = 0; i < ar->len; i++)
    {
      guint64 key = g_array_index (ar, guint64, i);

      g_hash_table_insert (seen, GSIZE_TO_POINTER (key), GSIZE_TO_POINTER (i));
    }

  g_printerr ("We have %u pages to graph\n", ar->len);

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, ar->len, (4096/16));
  cr = cairo_create (surface);

  cairo_set_line_width (cr, 1.0);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_rectangle (cr, 0, 0, ar->len, (4096/16));
  cairo_fill (cr);

  cairo_set_source_rgb (cr, 0, 0, 0);

  cairo_scale (cr, 1.0, 1.0/16.0);
  cairo_translate (cr, .5, .5);

  raxStart (&it, r);
  raxSeek (&it, "^", NULL, 0);
  while (raxNext (&it))
    {
      guint64 page;
      guint64 addr;
      guint64 size;
      guint x;
      guint y;

      memcpy (&addr, it.key, sizeof addr);
      page = addr / 4096;
      size = GPOINTER_TO_SIZE (it.data);

      x = GPOINTER_TO_UINT (g_hash_table_lookup (seen, GSIZE_TO_POINTER (page)));
      y = addr % 4096;

      /* TODO: Need size */

      cairo_move_to (cr, x, y);
      cairo_line_to (cr, x, y+size);
    }
  raxStop (&it);

  cairo_stroke (cr);

  cairo_surface_write_to_png (surface, "memory.png");

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  g_array_unref (ar);
  g_hash_table_unref (seen);

  g_main_loop_quit (main_loop);
}

gint
main (gint   argc,
      gchar *argv[])
{
  SysprofCaptureReader *reader;
  const gchar *filename = argv[1];
  g_autoptr(SysprofProfile) memprof = NULL;
  g_autoptr(GError) error = NULL;

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  main_loop = g_main_loop_new (NULL, FALSE);

  if (!(reader = sysprof_capture_reader_new (filename, &error)))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  memprof = sysprof_memprof_profile_new ();
  sysprof_profile_set_reader (memprof, reader);
  sysprof_profile_generate (memprof, NULL, generate_cb, NULL);

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  sysprof_capture_reader_unref (reader);

  return EXIT_SUCCESS;
}
