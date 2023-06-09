/* sysprof-weighted-callgraph-view.c
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

#include "sysprof-callgraph-view-private.h"
#include "sysprof-weighted-callgraph-view.h"

struct _SysprofWeightedCallgraphView
{
  SysprofCallgraphView parent_instance;
};

struct _SysprofWeightedCallgraphViewClass
{
  SysprofCallgraphViewClass parent_class;
};

typedef struct _AugmentWeight
{
  guint size;
  guint total;
} AugmentWeight;

G_DEFINE_FINAL_TYPE (SysprofWeightedCallgraphView, sysprof_weighted_callgraph_view, SYSPROF_TYPE_CALLGRAPH_VIEW)

static void
augment_weight (SysprofCallgraph     *callgraph,
                SysprofCallgraphNode *node,
                SysprofDocumentFrame *frame,
                gpointer              user_data)
{
  AugmentWeight *cur;
  AugmentWeight *sum;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (node != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_SAMPLE (frame));
  g_assert (user_data == NULL);

  cur = sysprof_callgraph_get_augment (callgraph, node);
  cur->size += 1;

  sum = sysprof_callgraph_get_summary_augment (callgraph, node);
  sum->size += 1;

  for (; node; node = sysprof_callgraph_node_parent (node))
    {
      cur = sysprof_callgraph_get_augment (callgraph, node);
      cur->total += 1;

      sum = sysprof_callgraph_get_summary_augment (callgraph, node);
      sum->size += 1;
    }
}

static void
sysprof_weighted_callgraph_view_class_init (SysprofWeightedCallgraphViewClass *klass)
{
  SysprofCallgraphViewClass *callgraph_view_class = SYSPROF_CALLGRAPH_VIEW_CLASS (klass);

  callgraph_view_class->augment_size = sizeof (AugmentWeight);
  callgraph_view_class->augment_func = augment_weight;
}

static void
sysprof_weighted_callgraph_view_init (SysprofWeightedCallgraphView *self)
{
}

GtkWidget *
sysprof_weighted_callgraph_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_WEIGHTED_CALLGRAPH_VIEW, NULL);
}
