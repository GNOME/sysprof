/* test-time-filter-model.c
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

#include <gtk/gtk.h>

#include "sysprof-time-filter-model.h"

static gint64
item_time (GObject  *item,
           gpointer  unused)
{
  return *(gint64 *)g_object_get_data (item, "time");
}

static GListStore *
create_source (const gint64 *times,
               guint         n_times)
{
  GListStore *store = g_list_store_new (G_TYPE_OBJECT);

  for (guint i = 0; i < n_times; i++)
    {
      g_autoptr(GObject) item = g_object_new (G_TYPE_OBJECT, NULL);
      gint64 *time = g_new (gint64, 1);

      *time = times[i];
      g_object_set_data_full (item, "time", time, g_free);
      g_list_store_append (store, item);
    }

  return store;
}

static SysprofTimeFilterModel *
create_filter (GListStore *source,
               gboolean    inclusive)
{
  g_autoptr(GtkExpression) expression = NULL;

  expression = gtk_cclosure_expression_new (G_TYPE_INT64,
                                            NULL,
                                            0,
                                            NULL,
                                            G_CALLBACK (item_time),
                                            NULL,
                                            NULL);
  return g_object_new (SYSPROF_TYPE_TIME_FILTER_MODEL,
                        "expression", expression,
                        "inclusive", inclusive,
                        "model", source,
                        NULL);
}

static void
assert_window (SysprofTimeFilterModel *filter,
               GListStore             *source,
               gint64                  begin,
               gint64                  end,
               guint                   first,
               guint                   count)
{
  SysprofTimeSpan span = { begin, end };

  sysprof_time_filter_model_set_time_span (filter, &span);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (filter)), ==, count);

  for (guint i = 0; i < count; i++)
    {
      g_autoptr(GObject) actual = g_list_model_get_item (G_LIST_MODEL (filter), i);
      g_autoptr(GObject) expected = g_list_model_get_item (G_LIST_MODEL (source), first + i);

      g_assert_true (actual == expected);
    }

  g_assert_null (g_list_model_get_item (G_LIST_MODEL (filter), count));
}

static void
test_boundaries (void)
{
  const gint64 times[] = { 10, 10, 20, 20, 20, 30, 30 };
  const struct {
    gint64 begin;
    gint64 end;
    guint first;
    guint count;
    guint inclusive_first;
    guint inclusive_count;
  } cases[] = {
    { 0, 9, 0, 0, 0, 1 },
    { 31, 40, 7, 0, 6, 1 },
    { 11, 19, 2, 0, 1, 2 },
    { 21, 29, 5, 0, 4, 2 },
    { 10, 10, 0, 2, 0, 3 },
    { 20, 20, 2, 3, 1, 5 },
    { 30, 30, 5, 2, 4, 3 },
    { 10, 20, 0, 5, 0, 6 },
    { 20, 30, 2, 5, 1, 6 },
    { 11, 29, 2, 3, 1, 5 },
    { 0, 20, 0, 5, 0, 6 },
    { 20, 40, 2, 5, 1, 6 },
    { 0, 40, 0, 7, 0, 7 },
    { 30, 10, 0, 0, 0, 0 },
    { G_MININT64, G_MAXINT64, 0, 7, 0, 7 },
  };
  g_autoptr(GListStore) source = create_source (times, G_N_ELEMENTS (times));

  for (guint inclusive = 0; inclusive < 2; inclusive++)
    {
      g_autoptr(SysprofTimeFilterModel) filter = create_filter (source, inclusive);

      for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
        {
          g_test_message ("case %u, inclusive %u", i, inclusive);
          assert_window (filter, source, cases[i].begin, cases[i].end,
                          inclusive ? cases[i].inclusive_first : cases[i].first,
                          inclusive ? cases[i].inclusive_count : cases[i].count);
        }
    }
}

static void
test_empty_singleton_extremes (void)
{
  const gint64 times[] = { G_MININT64, G_MININT64, G_MAXINT64, G_MAXINT64 };
  g_autoptr(GListStore) source = create_source (times, 0);
  g_autoptr(SysprofTimeFilterModel) filter = create_filter (source, FALSE);
  g_autoptr(GListStore) extremes = create_source (times, G_N_ELEMENTS (times));
  g_autoptr(GListStore) singleton = create_source (times, 1);

  assert_window (filter, source, 10, 20, 0, 0);
  g_object_set (filter,
                "inclusive", TRUE,
                NULL);
  assert_window (filter, source, 10, 20, 0, 0);
  sysprof_time_filter_model_set_model (filter, G_LIST_MODEL (singleton));
  assert_window (filter, singleton, G_MININT64, G_MININT64, 0, 1);
  g_object_set (filter,
                "inclusive", FALSE,
                NULL);
  assert_window (filter, singleton, 0, 1, 0, 0);
  assert_window (filter, singleton, G_MININT64, G_MININT64, 0, 1);
  sysprof_time_filter_model_set_model (filter, G_LIST_MODEL (extremes));
  assert_window (filter, extremes, G_MININT64, G_MININT64, 0, 2);
  assert_window (filter, extremes, G_MAXINT64, G_MAXINT64, 2, 2);
  assert_window (filter, extremes, G_MININT64 + 1, G_MAXINT64 - 1, 2, 0);
  sysprof_time_filter_model_set_time_span (filter, NULL);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (filter)), ==, 4);
  sysprof_time_filter_model_set_model (filter, NULL);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (filter)), ==, 0);
}

static void
test_source_changes (void)
{
  const gint64 times[] = { 10, 20, 20, 30 };
  g_autoptr(GListStore) source = create_source (times, G_N_ELEMENTS (times));
  g_autoptr(SysprofTimeFilterModel) filter = create_filter (source, FALSE);
  g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (source), 1);

  assert_window (filter, source, 20, 20, 1, 2);
  g_list_store_remove (source, 1);
  assert_window (filter, source, 20, 20, 1, 1);
  g_list_store_remove (source, 1);
  assert_window (filter, source, 20, 20, 1, 0);
  g_list_store_insert (source, 1, item);
  assert_window (filter, source, 20, 20, 1, 1);
  g_list_store_remove_all (source);
  assert_window (filter, source, 20, 20, 0, 0);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/time-filter/boundaries", test_boundaries);
  g_test_add_func ("/time-filter/empty-singleton-extremes", test_empty_singleton_extremes);
  g_test_add_func ("/time-filter/source-changes", test_source_changes);
  return g_test_run ();
}
