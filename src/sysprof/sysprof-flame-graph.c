/* sysprof-flame-graph.c
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
#include "sysprof-category-icon.h"
#include "sysprof-color-iter-private.h"

#include "sysprof-flame-graph.h"

struct _SysprofFlameGraph
{
  GtkWidget         parent_instance;

  SysprofCallgraph *callgraph;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofFlameGraph, sysprof_flame_graph, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_flame_graph_snapshot_node (GtkSnapshot           *snapshot,
                                   SysprofCallgraphNode  *node,
                                   const graphene_rect_t *area,
                                   double                 row_height,
                                   double                 min_width,
                                   const GdkRGBA         *default_color,
                                   int                    depth)
{
  SysprofCallgraphCategory category;
  const GdkRGBA *color;
  GdkRGBA fallback;

  g_assert (GTK_IS_SNAPSHOT (snapshot));
  g_assert (node != NULL);
  g_assert (area != NULL);

  category = SYSPROF_CALLGRAPH_CATEGORY_UNMASK (node->category);

  if (!(color = sysprof_callgraph_category_get_color (category)) || color->alpha == 0)
    {
      fallback = *default_color;
      fallback.alpha -= depth * .005,
      color = &fallback;
    }

  gtk_snapshot_append_color (snapshot,
                             color,
                             &GRAPHENE_RECT_INIT (area->origin.x,
                                                  area->origin.y + area->size.height - row_height,
                                                  area->size.width,
                                                  row_height));

  if (node->children != NULL)
    {
      graphene_rect_t child_area;
      guint64 weight = 0;

      child_area.origin.x = area->origin.x;
      child_area.origin.y = area->origin.y;
      child_area.size.height = area->size.height - row_height;

      for (SysprofCallgraphNode *child = node->children; child; child = child->next)
        weight += child->count;

      for (SysprofCallgraphNode *child = node->children; child; child = child->next)
        {
          double ratio = child->count / (double)weight;
          double width;

          width = ratio * area->size.width;

          if (width < min_width)
            child_area.size.width = min_width;
          else
            child_area.size.width = width;

          sysprof_flame_graph_snapshot_node (snapshot,
                                             child,
                                             &child_area,
                                             row_height,
                                             min_width,
                                             default_color,
                                             depth + 1);

          child_area.origin.x += width;
        }
    }
}

static void
sysprof_flame_graph_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)widget;
  SysprofColorIter iter;
  double row_height;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->callgraph == NULL)
    return;

  sysprof_color_iter_init (&iter);
  row_height = gtk_widget_get_height (widget) / (double)self->callgraph->height;

  sysprof_flame_graph_snapshot_node (snapshot,
                                     &self->callgraph->root,
                                     &GRAPHENE_RECT_INIT (0, 0,
                                                          gtk_widget_get_width (widget),
                                                          gtk_widget_get_height (widget)),
                                     row_height,
                                     1. / (double)gtk_widget_get_scale_factor (widget),
                                     sysprof_color_iter_next (&iter),
                                     0);
}

static void
sysprof_flame_graph_dispose (GObject *object)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)object;

  g_clear_object (&self->callgraph);

  G_OBJECT_CLASS (sysprof_flame_graph_parent_class)->dispose (object);
}

static void
sysprof_flame_graph_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofFlameGraph *self = SYSPROF_FLAME_GRAPH (object);

  switch (prop_id)
    {
    case PROP_CALLGRAPH:
      g_value_set_object (value, sysprof_flame_graph_get_callgraph (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_flame_graph_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofFlameGraph *self = SYSPROF_FLAME_GRAPH (object);

  switch (prop_id)
    {
    case PROP_CALLGRAPH:
      sysprof_flame_graph_set_callgraph (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_flame_graph_class_init (SysprofFlameGraphClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_flame_graph_dispose;
  object_class->get_property = sysprof_flame_graph_get_property;
  object_class->set_property = sysprof_flame_graph_set_property;

  widget_class->snapshot = sysprof_flame_graph_snapshot;

  properties[PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_flame_graph_init (SysprofFlameGraph *self)
{
}

GtkWidget *
sysprof_flame_graph_new (void)
{
  return g_object_new (SYSPROF_TYPE_FLAME_GRAPH, NULL);
}

SysprofCallgraph *
sysprof_flame_graph_get_callgraph (SysprofFlameGraph *self)
{
  g_return_val_if_fail (SYSPROF_IS_FLAME_GRAPH (self), NULL);

  return self->callgraph;
}

void
sysprof_flame_graph_set_callgraph (SysprofFlameGraph *self,
                                   SysprofCallgraph  *callgraph)
{
  g_return_if_fail (SYSPROF_IS_FLAME_GRAPH (self));
  g_return_if_fail (!callgraph || SYSPROF_IS_CALLGRAPH (callgraph));

  if (g_set_object (&self->callgraph, callgraph))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CALLGRAPH]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
