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

typedef struct _SysprofSymbolCacheNode SysprofSymbolCacheNode;
static void sysprof_symbol_cache_node_augment (SysprofSymbolCacheNode *node);
#define RB_AUGMENT(elem) sysprof_symbol_cache_node_augment(elem)

#include "tree.h"

#include "sysprof-bundled-symbolizer-private.h"
#include "sysprof-symbol-private.h"
#include "sysprof-symbol-cache-private.h"

struct _SysprofSymbolCacheNode
{
  RB_ENTRY(_SysprofSymbolCacheNode) link;
  SysprofSymbol *symbol;
  guint64 low;
  guint64 high;
  guint64 max;
};

struct _SysprofSymbolCache
{
  GObject parent_instance;
  RB_HEAD(sysprof_symbol_cache, _SysprofSymbolCacheNode) head;
};

G_DEFINE_FINAL_TYPE (SysprofSymbolCache, sysprof_symbol_cache, G_TYPE_OBJECT)

static inline int
sysprof_symbol_cache_node_compare (SysprofSymbolCacheNode *a,
                                   SysprofSymbolCacheNode *b)
{
  if (a->low < b->low)
    return -1;
  else if (a->low > b->low)
    return 1;
  else
    return 0;
}

RB_GENERATE_STATIC(sysprof_symbol_cache, _SysprofSymbolCacheNode, link, sysprof_symbol_cache_node_compare);

static void
sysprof_symbol_cache_node_augment (SysprofSymbolCacheNode *node)
{
  node->max = node->high;

  if (RB_LEFT(node, link) && RB_LEFT(node, link)->max > node->max)
    node->max = RB_LEFT(node, link)->max;

  if (RB_RIGHT(node, link) && RB_RIGHT(node, link)->max > node->max)
    node->max = RB_RIGHT(node, link)->max;
}

static void
sysprof_symbol_cache_node_finalize (SysprofSymbolCacheNode *node)
{
  g_clear_object (&node->symbol);
  g_free (node);
}

static void
sysprof_symbol_cache_node_free (SysprofSymbolCacheNode *node)
{
  SysprofSymbolCacheNode *right = RB_RIGHT(node, link);
  SysprofSymbolCacheNode *left = RB_LEFT(node, link);

  if (left != NULL)
    sysprof_symbol_cache_node_free (left);

  sysprof_symbol_cache_node_finalize (node);

  if (right != NULL)
    sysprof_symbol_cache_node_free (right);
}

static void
sysprof_symbol_cache_finalize (GObject *object)
{
  SysprofSymbolCache *self = (SysprofSymbolCache *)object;
  SysprofSymbolCacheNode *node = RB_ROOT(&self->head);

  if (node != NULL)
    sysprof_symbol_cache_node_free (node);

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
  RB_INIT (&self->head);
}

SysprofSymbolCache *
sysprof_symbol_cache_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYMBOL_CACHE, NULL);
}

void
sysprof_symbol_cache_take (SysprofSymbolCache *self,
                           SysprofSymbol      *symbol)
{
  SysprofSymbolCacheNode *node;
  SysprofSymbolCacheNode *parent;
  SysprofSymbolCacheNode *ret;

  g_return_if_fail (SYSPROF_IS_SYMBOL_CACHE (self));
  g_return_if_fail (SYSPROF_IS_SYMBOL (symbol));
  g_return_if_fail (symbol->end_address > symbol->begin_address);

  /* Some symbols are not suitable for our interval tree */
  if (symbol->begin_address == 0 ||
      symbol->end_address == 0 ||
      symbol->begin_address == symbol->end_address)
    {
      g_object_unref (symbol);
      return;
    }

  node = g_new0 (SysprofSymbolCacheNode, 1);
  node->symbol = symbol;
  node->low = symbol->begin_address;
  node->high = symbol->end_address-1;
  node->max = node->high;

  /* If there is a collision, then the node is returned. Otherwise
   * if the node was inserted, NULL is returned.
   */
  if ((ret = RB_INSERT(sysprof_symbol_cache, &self->head, node)))
    {
      sysprof_symbol_cache_node_free (node);
      return;
    }

  parent = RB_PARENT(node, link);

  while (parent != NULL)
    {
      if (node->max > parent->max)
        parent->max = node->max;
      node = parent;
      parent = RB_PARENT(parent, link);
    }
}

SysprofSymbol *
sysprof_symbol_cache_lookup (SysprofSymbolCache *self,
                             SysprofAddress      address)
{
  SysprofSymbolCacheNode *node;

  g_return_val_if_fail (SYSPROF_IS_SYMBOL_CACHE (self), NULL);

  if (address == 0)
    return NULL;

  node = RB_ROOT(&self->head);

  /* The root node contains our calculated max as augmented in RBTree.
   * Therefore, we can know if @address falls beyond the upper bound
   * in O(1) without having to add a branch to the while loop below.
   */
  if (node == NULL || node->max < address)
    return NULL;

  while (node != NULL)
    {
      g_assert (RB_LEFT(node, link) == NULL ||
                node->max >= RB_LEFT(node, link)->max);
      g_assert (RB_RIGHT(node, link) == NULL ||
                node->max >= RB_RIGHT(node, link)->max);

      if (address >= node->low && address <= node->high)
        return node->symbol;

      if (RB_LEFT(node, link) && RB_LEFT(node, link)->max >= address)
        node = RB_LEFT(node, link);
      else
        node = RB_RIGHT(node, link);
    }

  return NULL;
}

static guint
get_string (GByteArray *strings,
            GHashTable *strings_offset,
            const char *string)
{
  guint pos;

  if (string == NULL || string[0] == 0)
    return 0;

  pos = GPOINTER_TO_UINT (g_hash_table_lookup (strings_offset, string));

  if (pos == 0)
    {
      pos = strings->len;
      g_byte_array_append (strings, (const guint8 *)string, strlen (string) + 1);
      g_hash_table_insert (strings_offset, (char *)string, GUINT_TO_POINTER (pos));
    }

  return pos;
}

void
sysprof_symbol_cache_populate_packed (SysprofSymbolCache *self,
                                      GArray             *array,
                                      GByteArray         *strings,
                                      GHashTable         *strings_offset,
                                      int                 pid)
{
  SysprofSymbolCacheNode *node;

  g_return_if_fail (SYSPROF_IS_SYMBOL_CACHE (self));
  g_return_if_fail (array != NULL);

  RB_FOREACH(node, sysprof_symbol_cache, &self->head) {
    SysprofPackedSymbol packed;
    SysprofSymbol *symbol = node->symbol;

    if (symbol->is_fallback)
      continue;

    packed.addr_begin = symbol->begin_address;
    packed.addr_end = symbol->end_address;
    packed.pid = pid;
    packed.offset = get_string (strings, strings_offset, symbol->name);
    packed.tag_offset = get_string (strings, strings_offset, symbol->binary_nick);

    g_array_append_val (array, packed);
  }
}
