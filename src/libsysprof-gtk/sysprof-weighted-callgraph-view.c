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
#include "sysprof-progress-cell-private.h"

struct _SysprofWeightedCallgraphView
{
  SysprofCallgraphView parent_instance;

  GtkColumnViewColumn *self_column;
  GtkColumnViewColumn *total_column;

  GtkCustomSorter *self_sorter;
  GtkCustomSorter *total_sorter;
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
  cur->total += 1;

  sum = sysprof_callgraph_get_summary_augment (callgraph, node);
  sum->size += 1;
  sum->total += 1;

  for (node = sysprof_callgraph_node_parent (node);
       node != NULL;
       node = sysprof_callgraph_node_parent (node))
    {
      cur = sysprof_callgraph_get_augment (callgraph, node);
      cur->total += 1;

      sum = sysprof_callgraph_get_summary_augment (callgraph, node);
      sum->total += 1;
    }
}

static double
get_total_fraction (GObject *item)
{
  g_autoptr(GObject) row = NULL;

  g_object_get (item, "item", &row, NULL);

  if (GTK_IS_TREE_LIST_ROW (row))
    {
      GtkTreeListRow *tree_row = GTK_TREE_LIST_ROW (row);
      SysprofCallgraphFrame *frame = SYSPROF_CALLGRAPH_FRAME (gtk_tree_list_row_get_item (tree_row));
      SysprofCallgraph *callgraph = sysprof_callgraph_frame_get_callgraph (frame);
      AugmentWeight *sum = sysprof_callgraph_frame_get_augment (frame);
      AugmentWeight *root = sysprof_callgraph_get_augment (callgraph, NULL);

      return (double)sum->total / (double)root->total;
    }

  return 0;
}

static double
get_self_fraction (GObject *item)
{
  g_autoptr(GObject) row = NULL;

  g_object_get (item, "item", &row, NULL);

  if (GTK_IS_TREE_LIST_ROW (row))
    {
      GtkTreeListRow *tree_row = GTK_TREE_LIST_ROW (row);
      SysprofCallgraphFrame *frame = SYSPROF_CALLGRAPH_FRAME (gtk_tree_list_row_get_item (tree_row));
      SysprofCallgraph *callgraph = sysprof_callgraph_frame_get_callgraph (frame);
      AugmentWeight *sum = sysprof_callgraph_frame_get_augment (frame);
      AugmentWeight *root = sysprof_callgraph_get_augment (callgraph, NULL);

      return sum->size / (double)root->total;
    }

  return .0;
}

static int
sort_by_self (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
  SysprofCallgraphFrame *frame_a = (SysprofCallgraphFrame *)a;
  SysprofCallgraphFrame *frame_b = (SysprofCallgraphFrame *)b;
  AugmentWeight *aug_a = sysprof_callgraph_frame_get_augment (frame_a);
  AugmentWeight *aug_b = sysprof_callgraph_frame_get_augment (frame_b);
  AugmentWeight *root = user_data;
  double self_a = aug_a->size / (double)root->total;
  double self_b = aug_b->size / (double)root->total;

  if (self_a < self_b)
    return -1;
  else if (self_a > self_b)
    return 1;
  else
    return 0;
}

static int
sort_by_total (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  SysprofCallgraphFrame *frame_a = (SysprofCallgraphFrame *)a;
  SysprofCallgraphFrame *frame_b = (SysprofCallgraphFrame *)b;
  AugmentWeight *aug_a = sysprof_callgraph_frame_get_augment (frame_a);
  AugmentWeight *aug_b = sysprof_callgraph_frame_get_augment (frame_b);
  AugmentWeight *root = user_data;
  double total_a = aug_a->total / (double)root->total;
  double total_b = aug_b->total / (double)root->total;

  if (total_a < total_b)
    return -1;
  else if (total_a > total_b)
    return 1;
  else
    return 0;
}

static void
sysprof_weighted_callgraph_view_load (SysprofCallgraphView *view,
                                      SysprofCallgraph     *callgraph)
{
  SysprofWeightedCallgraphView *self = (SysprofWeightedCallgraphView *)view;
  AugmentWeight *root;

  g_assert (SYSPROF_IS_WEIGHTED_CALLGRAPH_VIEW (self));
  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  root = sysprof_callgraph_get_augment (callgraph, NULL);

  gtk_custom_sorter_set_sort_func (self->self_sorter, sort_by_self, root, NULL);
  gtk_custom_sorter_set_sort_func (self->total_sorter, sort_by_total, root, NULL);

  gtk_column_view_sort_by_column (SYSPROF_CALLGRAPH_VIEW (self)->column_view,
                                  self->total_column,
                                  GTK_SORT_DESCENDING);
}

static void
sysprof_weighted_callgraph_view_class_init (SysprofWeightedCallgraphViewClass *klass)
{
  SysprofCallgraphViewClass *callgraph_view_class = SYSPROF_CALLGRAPH_VIEW_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  callgraph_view_class->augment_size = sizeof (AugmentWeight);
  callgraph_view_class->augment_func = augment_weight;
  callgraph_view_class->load = sysprof_weighted_callgraph_view_load;

  gtk_widget_class_set_template_from_resource (widget_class, "/libsysprof-gtk/sysprof-weighted-callgraph-view.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofWeightedCallgraphView, self_column);
  gtk_widget_class_bind_template_child (widget_class, SysprofWeightedCallgraphView, total_column);
  gtk_widget_class_bind_template_child (widget_class, SysprofWeightedCallgraphView, self_sorter);
  gtk_widget_class_bind_template_child (widget_class, SysprofWeightedCallgraphView, total_sorter);
  gtk_widget_class_bind_template_callback (widget_class, get_self_fraction);
  gtk_widget_class_bind_template_callback (widget_class, get_total_fraction);

  g_type_ensure (SYSPROF_TYPE_PROGRESS_CELL);
}

static void
sysprof_weighted_callgraph_view_init (SysprofWeightedCallgraphView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_weighted_callgraph_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_WEIGHTED_CALLGRAPH_VIEW, NULL);
}
