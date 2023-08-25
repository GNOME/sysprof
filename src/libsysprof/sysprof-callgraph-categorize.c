/* sysprof-callgraph-categorize.c
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

#include "sysprof-callgraph-private.h"
#include "sysprof-categories-private.h"
#include "sysprof-symbol-private.h"

SysprofCallgraphCategory
_sysprof_callgraph_node_categorize (SysprofCallgraphNode *node)
{
  SysprofSymbol *symbol;
  SysprofCallgraphCategory category;

  g_return_val_if_fail (node, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);
  g_return_val_if_fail (node->summary, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);
  g_return_val_if_fail (node->summary->symbol, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);

  symbol = node->summary->symbol;

  /* NOTE: We could certainly extend this to allow users to define custom
   * categorization. Additionally, we might want to match more than one node so
   * that you can do valgrind style function matches like:
   */

  if (symbol->binary_nick == NULL)
    return SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;

  category = sysprof_categories_lookup (NULL, symbol->binary_nick, symbol->name);

  if (category == 0)
    return SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;

  return category;
}
