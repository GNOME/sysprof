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
#include "sysprof-document-bitset-index-private.h"

typedef GObject TestSource;
typedef GObjectClass TestSourceClass;

static GType
source_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static guint
source_size (GListModel *model)
{
  return 200000;
}

static gpointer
source_item (GListModel *model,
             guint       position)
{
  GObject *item;

  if (position >= source_size (model))
    return NULL;

  item = g_object_new (G_TYPE_OBJECT, NULL);
  g_object_set_data (item, "position", GUINT_TO_POINTER (position));
  return item;
}

static void
source_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = source_type;
  iface->get_n_items = source_size;
  iface->get_item = source_item;
}

G_DEFINE_TYPE_WITH_CODE (TestSource, test_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, source_iface_init))

static void
test_source_class_init (TestSourceClass *klass)
{
}

static void
test_source_init (TestSource *self)
{
}

typedef struct
{
  GListModel *model;
  GArray     *expected;
} Reads;

static gpointer
read_items (gpointer data)
{
  Reads *reads = data;
  guint n = reads->expected->len;
  guint random = 12345;

  g_assert_null (g_list_model_get_item (reads->model, n));
  g_assert_null (g_list_model_get_item (reads->model, G_MAXUINT));
  g_assert_cmpuint (g_list_model_get_n_items (reads->model), ==, n);
  g_assert_cmpuint (g_list_model_get_item_type (reads->model), ==, G_TYPE_OBJECT);

  for (guint pass = 0; pass < 4; pass++)
    for (guint i = 0; i < n; i++)
      {
        guint position = i;
        g_autoptr(GObject) item = NULL;
        g_autoptr(GObject) repeated = NULL;

        random = random * 1664525 + 1013904223;
        if (pass == 1)
          position = n - i - 1;
        else if (pass > 1)
          position = random % n;

        item = g_list_model_get_item (reads->model, position);
        repeated = g_list_model_get_item (reads->model, position);
        g_assert_nonnull (item);
        g_assert_nonnull (repeated);
        g_assert_cmpuint (GPOINTER_TO_UINT (g_object_get_data (item, "position")), ==,
                          g_array_index (reads->expected, guint, position));
        g_assert_cmpuint (GPOINTER_TO_UINT (g_object_get_data (repeated, "position")), ==,
                          g_array_index (reads->expected, guint, position));
      }

  return NULL;
}

static void
test_index (void)
{
  g_autoptr(GObject) source = g_object_new (test_source_get_type (), NULL);

  for (guint mode = 0; mode < 4; mode++)
    {
      g_autoptr(EggBitset) bits = egg_bitset_new_empty ();
      g_autoptr(GListModel) model = NULL;
      g_autoptr(GArray) expected = g_array_new (FALSE, FALSE, sizeof (guint));
      GThread *threads[4];
      Reads reads;

      for (guint i = 0; i < 200000; i++)
        if ((mode == 1 &&
             i == 65536) ||
            (mode == 2 &&
             (i % 997 == 0 ||
              i == 65535 ||
              i == 65536)) ||
            (mode == 3 &&
             i >= 60000 &&
             i < 140000))
          {
            egg_bitset_add (bits, i);
            g_array_append_val (expected, i);
          }

      model = _sysprof_document_bitset_index_new (G_LIST_MODEL (source), bits);
      egg_bitset_remove_all (bits);
      reads.model = model;
      reads.expected = expected;
      read_items (&reads);

      for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
        threads[i] = g_thread_new ("bitset-reader", read_items, &reads);
      for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
        g_thread_join (threads[i]);
    }
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/document/bitset-index", test_index);
  return g_test_run ();
}
