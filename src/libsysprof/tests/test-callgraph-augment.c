/* test-callgraph-augment.c
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

#include "sysprof-callgraph-private.h"

static void
test_augment (void)
{
  const gsize sizes[] = { 0, sizeof (gpointer), 2 * sizeof (gpointer), 128 };

  for (guint i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
      g_autoptr(SysprofCallgraph) callgraph = NULL;
      SysprofCallgraphNode *node;
      guint8 *root_data;
      guint8 *node_data;

      callgraph = g_object_new (SYSPROF_TYPE_CALLGRAPH, NULL);
      callgraph->augment_size = sizes[i];
      node = sysprof_allocator_new0 (callgraph->allocator, SysprofCallgraphNode);
      root_data = sysprof_callgraph_get_augment (callgraph, NULL);
      node_data = sysprof_callgraph_get_augment (callgraph, node);

      if (sizes[i] == 0)
        {
          g_assert_null (root_data);
          g_assert_null (node_data);
          continue;
        }

      g_assert_true (root_data != node_data);

      for (gsize j = 0; j < sizes[i]; j++)
        {
          g_assert_cmpuint (root_data[j], ==, 0);
          g_assert_cmpuint (node_data[j], ==, 0);
          root_data[j] = 0x12;
          node_data[j] = 0x34;
        }

      g_assert_true (root_data == sysprof_callgraph_get_augment (callgraph, NULL));
      g_assert_true (node_data == sysprof_callgraph_get_augment (callgraph, node));
      g_assert_cmpuint (root_data[sizes[i] - 1], ==, 0x12);
      g_assert_cmpuint (node_data[sizes[i] - 1], ==, 0x34);
    }
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/callgraph/augment", test_augment);
  return g_test_run ();
}
