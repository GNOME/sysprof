/* sysprof-symbol-cache.c
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

#include "sysprof-symbol-private.h"
#include "sysprof-symbol-cache-private.h"

struct _SysprofSymbolCache
{
  GObject    parent_instance;
  GSequence *symbols;
};

G_DEFINE_FINAL_TYPE (SysprofSymbolCache, sysprof_symbol_cache, G_TYPE_OBJECT)

static void
sysprof_symbol_cache_finalize (GObject *object)
{
  SysprofSymbolCache *self = (SysprofSymbolCache *)object;

  g_clear_pointer (&self->symbols, g_sequence_free);

  G_OBJECT_CLASS (sysprof_symbol_cache_parent_class)->finalize (object);
}

static void
sysprof_symbol_cache_class_init (SysprofSymbolCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_symbol_cache_finalize;
}

static void
sysprof_symbol_cache_init (SysprofSymbolCache *self)
{
  self->symbols = g_sequence_new (g_object_unref);
}

SysprofSymbolCache *
sysprof_symbol_cache_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYMBOL_CACHE, NULL);
}

static int
sysprof_symbol_cache_compare (gconstpointer a,
                              gconstpointer b,
                              gpointer      user_data)
{
  const SysprofSymbol *sym_a = a;
  const SysprofSymbol *sym_b = b;

  if (sym_a->begin_address < sym_b->begin_address)
    return -1;

  if (sym_a->begin_address > sym_b->end_address)
    return 1;

  return 0;
}

/**
 * sysprof_symbol_cache_take:
 * @self: a #SysprofSymbolCache
 * @symbol: (transfer full): a #SysprofSymbol
 *
 */
void
sysprof_symbol_cache_take (SysprofSymbolCache *self,
                           SysprofSymbol      *symbol)
{
  g_return_if_fail (SYSPROF_IS_SYMBOL_CACHE (self));
  g_return_if_fail (SYSPROF_IS_SYMBOL (symbol));

  if (symbol->begin_address == 0 || symbol->end_address == 0)
    return;

  g_sequence_insert_sorted (self->symbols,
                            g_object_ref (symbol),
                            sysprof_symbol_cache_compare,
                            NULL);
}

static int
sysprof_symbol_cache_lookup_func (gconstpointer a,
                                  gconstpointer b,
                                  gpointer      user_data)
{
  const SysprofSymbol *sym_a = a;
  const gint64 *addr = b;

  if (*addr < sym_a->begin_address)
    return 1;

  if (*addr > sym_a->end_address)
    return -1;

  return 0;
}

SysprofSymbol *
sysprof_symbol_cache_lookup (SysprofSymbolCache *self,
                             SysprofAddress      address)
{
  GSequenceIter *iter;

  g_return_val_if_fail (SYSPROF_IS_SYMBOL_CACHE (self), NULL);

  if (address == 0)
    return NULL;

  iter = g_sequence_lookup (self->symbols,
                            &address,
                            sysprof_symbol_cache_lookup_func,
                            NULL);

  if (iter != NULL)
    return g_sequence_get (iter);

  return NULL;
}
