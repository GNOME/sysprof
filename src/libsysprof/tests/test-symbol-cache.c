/* test-symbol-cache.c
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

#include <stdlib.h>

#include <sysprof.h>

#include "sysprof-symbol-private.h"
#include "sysprof-symbol-cache-private.h"

typedef struct _SymbolInfo
{
  const char *name;
  guint64 begin;
  guint64 end;
  int position;
  int sort;
  SysprofSymbol *symbol;
} SymbolInfo;

static SysprofSymbol *
create_symbol (const char *name,
               guint64     begin,
               guint64     end)
{
  g_assert (begin < end);

  return _sysprof_symbol_new (g_ref_string_new (name), NULL, NULL, begin, end, SYSPROF_SYMBOL_KIND_USER);
}

static int
sort_by_key (gconstpointer a,
             gconstpointer b)
{
  const SymbolInfo *info_a = a;
  const SymbolInfo *info_b = b;

  if (info_a->sort < info_b->sort)
    return -1;
  else if (info_a->sort > info_b->sort)
    return 1;
  else
    return 0;
}

static int
sort_by_position (gconstpointer a,
                  gconstpointer b)
{
  const SymbolInfo *info_a = a;
  const SymbolInfo *info_b = b;

  if (info_a->position < info_b->position)
    return -1;
  else if (info_a->position > info_b->position)
    return 1;
  else
    return 0;
}

static void
test_interval_tree (void)
{
  SysprofSymbolCache *symbol_cache = sysprof_symbol_cache_new ();
  SymbolInfo symbols[] = {
    { "symbol1", 0x10000, 0x20000 },
    { "symbol2", 0x20000, 0x30000 },
    { "symbol3", 0x30000, 0x40000 },
    { "symbol4", 0x90000, 0xa0000 },
    { "symbol5", 0xb0000, 0xb0001 },
    { "symbol6", 0xb0001, 0xb0002 },
  };

  /* Add some randomness on insertion */
  for (guint i = 0; i < G_N_ELEMENTS (symbols); i++)
    {
      symbols[i].position = i;
      symbols[i].sort = g_random_int ();
    }

  /* Sort randomly for insertion */
  qsort (symbols, G_N_ELEMENTS (symbols), sizeof (SymbolInfo), sort_by_key);
  for (guint i = 0; i < G_N_ELEMENTS (symbols); i++)
    {
      SymbolInfo *info = &symbols[i];

      g_assert_cmpint (info->begin, <, info->end);

      info->symbol = create_symbol (info->name, info->begin, info->end);

      g_assert_nonnull (info->symbol);
      g_assert_true (SYSPROF_IS_SYMBOL (info->symbol));

      sysprof_symbol_cache_take (symbol_cache, g_object_ref (info->symbol));
    }

  /* Now resort to do lookups with edge checking */
  qsort (symbols, G_N_ELEMENTS (symbols), sizeof (SymbolInfo), sort_by_position);
  for (guint i = 0; i < G_N_ELEMENTS (symbols); i++)
    {
      const SymbolInfo *info = &symbols[i];
      const SymbolInfo *prev = i > 0 ? &symbols[i-1] : NULL;
      const SymbolInfo *next = i + 1 < G_N_ELEMENTS (symbols) ? &symbols[i+1] : NULL;
      SysprofSymbol *lookup;

      g_assert_cmpint (info->position, ==, i);

      lookup = sysprof_symbol_cache_lookup (symbol_cache, info->begin-1);
      if (prev && info->begin == prev->end)
        g_assert_true (lookup == prev->symbol);
      else
        g_assert_null (lookup);

      lookup = sysprof_symbol_cache_lookup (symbol_cache, info->begin);
      g_assert_nonnull (lookup);
      g_assert_true (lookup == info->symbol);

      lookup = sysprof_symbol_cache_lookup (symbol_cache, info->end);
      if (next == NULL || next->begin > info->end)
        g_assert_null (lookup);
      else
        g_assert_true (lookup == next->symbol);

      if (info->begin+1 != info->end)
        {
          lookup = sysprof_symbol_cache_lookup (symbol_cache, info->begin+1);
          g_assert_nonnull (lookup);
          g_assert_true (lookup == info->symbol);
        }

      lookup = sysprof_symbol_cache_lookup (symbol_cache, info->end-1);
      g_assert_nonnull (lookup);
      g_assert_true (lookup == info->symbol);

      lookup = sysprof_symbol_cache_lookup (symbol_cache, info->begin + ((info->end-info->begin)/2));
      g_assert_nonnull (lookup);
      g_assert_true (lookup == info->symbol);
    }

  g_assert_finalize_object (symbol_cache);

  for (guint i = 0; i < G_N_ELEMENTS (symbols); i++)
    g_assert_finalize_object (symbols[i].symbol);
}

static void
test_jitmap (void)
{
  SysprofSymbolCache *symbol_cache = sysprof_symbol_cache_new ();

  for (guint i = 1; i <= 10000; i++)
    {
      SysprofAddress begin = 0xE000000000000000 + i;
      g_autofree char *name = g_strdup_printf ("%u", i);
      SysprofSymbol *symbol = create_symbol (name, begin, begin+1);

      sysprof_symbol_cache_take (symbol_cache, symbol);
    }

  g_assert_null (sysprof_symbol_cache_lookup (symbol_cache, 0));
  g_assert_null (sysprof_symbol_cache_lookup (symbol_cache, 10001));

  for (guint i = 1; i <= 10000; i++)
    {
      SysprofAddress begin = 0xE000000000000000 + i;
      SysprofSymbol *symbol = sysprof_symbol_cache_lookup (symbol_cache, begin);
      g_autofree char *name = g_strdup_printf ("%u", i);

      g_assert_nonnull (symbol);
      g_assert_cmpint (begin, ==, symbol->begin_address);
      g_assert_cmpint (begin+1, ==, symbol->end_address);
      g_assert_cmpstr (name, ==, symbol->name);
    }

  g_assert_finalize_object (symbol_cache);
}

static void
test_collision (void)
{
  SysprofSymbolCache *symbol_cache = sysprof_symbol_cache_new ();
  SysprofSymbol *first = NULL;

  for (guint i = 1; i <= 10000; i++)
    {
      SysprofAddress begin = 0xe000000000000000 + i;
      g_autofree char *name = g_strdup_printf ("%u", i);
      SysprofSymbol *symbol = create_symbol (name, begin, begin+1);

      if (first == NULL)
        first = g_object_ref (symbol);

      sysprof_symbol_cache_take (symbol_cache, symbol);
    }

  g_assert_true (SYSPROF_IS_SYMBOL (first));

  for (guint i = 1; i <= 10000; i++)
    {
      SysprofAddress begin = 0xE000000000000000 + i;
      g_autofree char *name = g_strdup_printf ("%u", i);
      SysprofSymbol *symbol = create_symbol (name, begin, begin+1);

      sysprof_symbol_cache_take (symbol_cache, symbol);
    }

  g_assert_finalize_object (symbol_cache);
  g_assert_finalize_object (first);

  /* To test this fully, you need `-Db_sanitize=address` so that
   * you can detect any leaks from RB_INSERT().
   */
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/libsysprof/SysprofSymbolCache/interval-tree",
                   test_interval_tree);
  g_test_add_func ("/libsysprof/SysprofSymbolCache/jitmap",
                   test_jitmap);
  g_test_add_func ("/libsysprof/SysprofSymbolCache/collision",
                   test_collision);
  return g_test_run ();
}
