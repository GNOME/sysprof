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

#include "sysprof-symbol-private.h"

#include "sysprof-flame-graph.h"

#define ROW_HEIGHT 14

typedef struct
{
  SysprofCallgraphNode *node;
  guint16 x;
  guint16 y;
  guint16 w;
  guint16 h;
} FlameRectangle;

struct _SysprofFlameGraph
{
  GtkWidget         parent_instance;

  SysprofCallgraph *callgraph;
  GskRenderNode    *rendered;
  GArray           *nodes;

  double            motion_x;
  double            motion_y;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofFlameGraph, sysprof_flame_graph, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

typedef struct
{
  graphene_point_t point;
  int width;
} FlameSearch;

static int
search_compare (gconstpointer key,
                gconstpointer item)
{
  const FlameSearch *search = key;
  const FlameRectangle *rect = item;
  graphene_rect_t area;

  area = GRAPHENE_RECT_INIT (rect->x / (double)G_MAXUINT16 * search->width,
                             rect->y,
                             rect->w / (double)G_MAXUINT16 * search->width,
                             rect->h);

  if (search->point.y < area.origin.y)
    return -1;

  if (search->point.y > area.origin.y + area.size.height)
    return 1;

  if (search->point.x < area.origin.x)
    return -1;

  if (search->point.x > area.origin.x + area.size.width)
    return 1;

  return 0;
}

static FlameRectangle *
find_node_at_coord (SysprofFlameGraph *self,
                    double             x,
                    double             y)
{
  FlameSearch search;

  if (self->nodes == NULL)
    return NULL;

  search.point.x = x;
  search.point.y = y;
  search.width = gtk_widget_get_width (GTK_WIDGET (self));

  return bsearch (&search, self->nodes->data, self->nodes->len, sizeof (FlameRectangle), search_compare);
}

static void
sysprof_flame_graph_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)widget;
  FlameRectangle *highlight = NULL;
  graphene_point_t point;
  int width;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->nodes == NULL || self->nodes->len == 0)
    return;

  point = GRAPHENE_POINT_INIT (self->motion_x, self->motion_y);
  width = gtk_widget_get_width (widget);

  if (self->rendered == NULL)
    {
      GtkSnapshot *child_snapshot = gtk_snapshot_new ();
      const GdkRGBA *default_color;
      PangoLayout *layout;
      SysprofColorIter iter;

      sysprof_color_iter_init (&iter);
      default_color = sysprof_color_iter_next (&iter);

      layout = gtk_widget_create_pango_layout (widget, NULL);
      pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);

      for (guint i = 0; i < self->nodes->len; i++)
        {
          FlameRectangle *rect = &g_array_index (self->nodes, FlameRectangle, i);
          SysprofCallgraphCategory category = SYSPROF_CALLGRAPH_CATEGORY_UNMASK (rect->node->category);
          const GdkRGBA *color = sysprof_callgraph_category_get_color (category);
          graphene_rect_t area;

          if (color == NULL)
            color = default_color;

          area = GRAPHENE_RECT_INIT (rect->x / (double)G_MAXUINT16 * width,
                                     rect->y,
                                     rect->w / (double)G_MAXUINT16 * width,
                                     rect->h);

          if (!highlight && graphene_rect_contains_point (&area, &point))
            highlight = rect;

          gtk_snapshot_append_color (child_snapshot, color, &area);

          if (area.size.width > ROW_HEIGHT)
            {
              pango_layout_set_text (layout, rect->node->summary->symbol->name, -1);
              pango_layout_set_width (layout, PANGO_SCALE * area.size.width - 4);

              gtk_snapshot_save (child_snapshot);
              gtk_snapshot_translate (child_snapshot,
                                      &GRAPHENE_POINT_INIT (round (area.origin.x) + 2,
                                                            area.origin.y + area.size.height - ROW_HEIGHT));
              gtk_snapshot_append_layout (child_snapshot, layout, &(GdkRGBA) {1,1,1,1});
              gtk_snapshot_restore (child_snapshot);
            }
        }

      self->rendered = gtk_snapshot_free_to_node (child_snapshot);

      g_object_unref (layout);
    }

  if (self->motion_x || self->motion_y)
    {
      if (highlight == NULL)
        highlight = find_node_at_coord (self, self->motion_x, self->motion_y);
    }

  gtk_snapshot_append_node (snapshot, self->rendered);

  if (highlight)
    {
      graphene_rect_t area;

      area = GRAPHENE_RECT_INIT (highlight->x / (double)G_MAXUINT16 * width,
                                 highlight->y,
                                 highlight->w / (double)G_MAXUINT16 * width,
                                 highlight->h);

      gtk_snapshot_append_color (snapshot, &(GdkRGBA) {1,1,1,.25}, &area);
    }
}

static void
sysprof_flame_graph_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)widget;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  *minimum_baseline = -1;
  *natural_baseline = -1;

  if (orientation == GTK_ORIENTATION_VERTICAL)
    {
      if (self->callgraph != NULL)
        *minimum = ROW_HEIGHT * self->callgraph->height + self->callgraph->height + 1;

      *natural = *minimum;
    }
  else
    {
      *minimum = 1;
      *natural = 500;
    }
}

static void
sysprof_flame_graph_motion_enter_cb (SysprofFlameGraph        *self,
                                     double                    x,
                                     double                    y,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = x;
  self->motion_y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_flame_graph_motion_motion_cb (SysprofFlameGraph        *self,
                                      double                    x,
                                      double                    y,
                                      GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = x;
  self->motion_y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_flame_graph_motion_leave_cb (SysprofFlameGraph        *self,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  self->motion_x = 0;
  self->motion_y = 0;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_flame_graph_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)widget;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  g_clear_pointer (&self->rendered, gsk_render_node_unref);
}

static void
sysprof_flame_graph_dispose (GObject *object)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)object;

  g_clear_pointer (&self->rendered, gsk_render_node_unref);
  g_clear_pointer (&self->nodes, g_array_unref);
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
  widget_class->measure = sysprof_flame_graph_measure;
  widget_class->size_allocate = sysprof_flame_graph_size_allocate;

  properties[PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "flamegraph");
}

static void
sysprof_flame_graph_init (SysprofFlameGraph *self)
{
  GtkEventController *motion;

  gtk_widget_add_css_class (GTK_WIDGET (self), "view");

  motion = gtk_event_controller_motion_new ();
  g_signal_connect_object (motion,
                           "enter",
                           G_CALLBACK (sysprof_flame_graph_motion_enter_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (motion,
                           "leave",
                           G_CALLBACK (sysprof_flame_graph_motion_leave_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (motion,
                           "motion",
                           G_CALLBACK (sysprof_flame_graph_motion_motion_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), motion);
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

static void
sysprof_flame_graph_generate_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  g_autoptr(SysprofFlameGraph) self = user_data;
  g_autoptr(GArray) nodes = NULL;
  GTask *task = (GTask *)result;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (G_IS_TASK (task));

  if ((nodes = g_task_propagate_pointer (task, NULL)))
    {
      g_clear_pointer (&self->nodes, g_array_unref);
      self->nodes = g_steal_pointer (&nodes);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
generate (GArray                *array,
          SysprofCallgraphNode  *node,
          const graphene_rect_t *area)
{
  FlameRectangle rect;

  g_assert (array != NULL);
  g_assert (node != NULL);
  g_assert (area != NULL);

  rect.x = area->origin.x;
  rect.y = area->origin.y + area->size.height - ROW_HEIGHT - 1;
  rect.w = area->size.width;
  rect.h = ROW_HEIGHT;
  rect.node = node;

  g_array_append_val (array, rect);

  if (node->children != NULL)
    {
      graphene_rect_t child_area;
      guint64 weight = 0;

      child_area.origin.x = area->origin.x;
      child_area.origin.y = area->origin.y;
      child_area.size.height = area->size.height - ROW_HEIGHT - 1;

      for (SysprofCallgraphNode *child = node->children; child; child = child->next)
        weight += child->count;

      for (SysprofCallgraphNode *child = node->children; child; child = child->next)
        {
          double ratio = child->count / (double)weight;
          double width;

          width = ratio * area->size.width;

          child_area.size.width = width;

          generate (array, child, &child_area);

          child_area.origin.x += width;
        }
    }
}

static int
sort_by_coord (gconstpointer a,
               gconstpointer b)
{
  const FlameRectangle *rect_a = a;
  const FlameRectangle *rect_b = b;

  if (rect_a->y < rect_b->y)
    return -1;

  if (rect_a->y > rect_b->y)
    return 1;

  if (rect_a->x < rect_b->x)
    return -1;

  if (rect_a->x > rect_b->x)
    return 1;

  return 0;
}

static void
sysprof_flame_graph_generate_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  SysprofCallgraph *callgraph = task_data;
  g_autoptr(GArray) array = NULL;
  int height;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));

  array = g_array_sized_new (FALSE, FALSE, sizeof (FlameRectangle), callgraph->root.count);
  height = callgraph->height * ROW_HEIGHT + callgraph->height + 1;

  generate (array,
            &callgraph->root,
            &GRAPHENE_RECT_INIT (0, 0, G_MAXUINT16, height));

  g_array_sort (array, sort_by_coord);

  g_task_return_pointer (task, g_steal_pointer (&array), (GDestroyNotify)g_array_unref);
}

void
sysprof_flame_graph_set_callgraph (SysprofFlameGraph *self,
                                   SysprofCallgraph  *callgraph)
{
  g_return_if_fail (SYSPROF_IS_FLAME_GRAPH (self));
  g_return_if_fail (!callgraph || SYSPROF_IS_CALLGRAPH (callgraph));

  if (g_set_object (&self->callgraph, callgraph))
    {
      g_clear_pointer (&self->rendered, gsk_render_node_unref);
      g_clear_pointer (&self->nodes, g_array_unref);

      if (callgraph != NULL)
        {
          g_autoptr(GTask) task = NULL;

          task = g_task_new (NULL, NULL, sysprof_flame_graph_generate_cb, g_object_ref (self));
          g_task_set_task_data (task, g_object_ref (callgraph), g_object_unref);
          g_task_run_in_thread (task, sysprof_flame_graph_generate_worker);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CALLGRAPH]);

      gtk_widget_queue_resize (GTK_WIDGET (self));
    }
}
