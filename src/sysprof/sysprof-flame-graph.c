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

#include <glib/gi18n.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-animation.h"
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
  GtkWidget             parent_instance;

  SysprofCallgraph     *callgraph;
  GskRenderNode        *rendered;
  GArray               *nodes;
  SysprofCallgraphNode *root;
  FlameRectangle       *under_pointer;

  GListModel           *utility_traceables;

  SysprofAnimation     *animation;

  double                motion_x;
  double                motion_y;

  guint                 queued_scroll;

  guint                 did_animation : 1;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  PROP_UTILITY_TRACEABLES,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofFlameGraph, sysprof_flame_graph, GTK_TYPE_WIDGET)

static void sysprof_flame_graph_invalidate (SysprofFlameGraph *self);

static GParamSpec *properties [N_PROPS];

static void
sysprof_flame_graph_set_utility_traceables (SysprofFlameGraph *self,
                                            GListModel        *model)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->utility_traceables, model))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UTILITY_TRACEABLES]);
}

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
  FlameRectangle *ret;

  if (self->nodes == NULL)
    return NULL;

  search.point.x = x;
  search.point.y = y;
  search.width = gtk_widget_get_width (GTK_WIDGET (self));

  ret = bsearch (&search, self->nodes->data, self->nodes->len, sizeof (FlameRectangle), search_compare);

  if (ret == NULL)
    {
      /* Try to recover from pointer inbetween rows */
      search.point.y -= 1;

      ret = bsearch (&search, self->nodes->data, self->nodes->len, sizeof (FlameRectangle), search_compare);
    }

  return ret;
}

static void
sysprof_flame_graph_animation_done (gpointer data)
{
  g_autoptr(SysprofFlameGraph) self = data;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  self->did_animation = TRUE;
}

static gboolean
sysprof_flame_graph_do_scroll (gpointer data)
{
  SysprofFlameGraph *self = data;
  SysprofAnimation *animation;
  GtkAdjustment *adj;
  GtkWidget *scroller;
  double upper;
  double page_size;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  self->queued_scroll = 0;

  if (self->animation)
    {
      sysprof_animation_stop (self->animation);
      g_clear_weak_pointer (&self->animation);
    }

  if (!(scroller = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_SCROLLED_WINDOW)) ||
      !(adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroller))))
    return G_SOURCE_REMOVE;

  upper = gtk_adjustment_get_upper (adj);
  page_size = gtk_adjustment_get_page_size (adj);

  if (page_size > upper)
    return G_SOURCE_REMOVE;

  animation = sysprof_object_animate_full (adj,
                                           SYSPROF_ANIMATION_EASE_IN_OUT_CUBIC,
                                           250,
                                           gtk_widget_get_frame_clock (GTK_WIDGET (self)),
                                           sysprof_flame_graph_animation_done,
                                           g_object_ref (self),
                                           "value", upper - page_size,
                                           NULL);

  g_set_weak_pointer (&self->animation, animation);

  return G_SOURCE_REMOVE;
}

static void
sysprof_flame_graph_queue_scroll (SysprofFlameGraph *self)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  if (self->did_animation)
    return;

  if (self->queued_scroll == 0)
    self->queued_scroll = g_timeout_add (150, sysprof_flame_graph_do_scroll, self);
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
          const GdkRGBA *category_color = sysprof_callgraph_category_get_color (category);
          graphene_rect_t area;
          GdkRGBA color;

          if (category_color == NULL)
            color = *default_color;
          else
            color = *category_color;

          color.alpha = .6 + (g_str_hash (rect->node->summary->symbol->name) % 1000) / 2500.;

          area = GRAPHENE_RECT_INIT (rect->x / (double)G_MAXUINT16 * width,
                                     rect->y,
                                     rect->w / (double)G_MAXUINT16 * width,
                                     rect->h);

          if (area.size.width < .25)
            continue;

          if (!highlight && graphene_rect_contains_point (&area, &point))
            highlight = rect;

          gtk_snapshot_append_color (child_snapshot, &color, &area);

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

  gtk_snapshot_append_node (snapshot, self->rendered);

  if (self->motion_x || self->motion_y)
    {
      double y = self->motion_y;

      if (highlight == NULL)
        highlight = find_node_at_coord (self, self->motion_x, y);

      while (highlight)
        {
          graphene_rect_t area;

          area = GRAPHENE_RECT_INIT (highlight->x / (double)G_MAXUINT16 * width,
                                     highlight->y,
                                     highlight->w / (double)G_MAXUINT16 * width,
                                     highlight->h);

          gtk_snapshot_append_color (snapshot, &(GdkRGBA) {1,1,1,.25}, &area);

          y += ROW_HEIGHT + 1;
          highlight = find_node_at_coord (self, self->motion_x, y);
        }
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
sysprof_flame_graph_set_motion (SysprofFlameGraph *self,
                                double             x,
                                double             y)
{
  FlameRectangle *rect;

  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  self->motion_x = x;
  self->motion_y = y;

  rect = find_node_at_coord (self, x, y);

  if (rect != self->under_pointer)
    {
      self->under_pointer = rect;

      gtk_widget_set_cursor_from_name (GTK_WIDGET (self), rect ? "pointer" : NULL);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_flame_graph_motion_enter_cb (SysprofFlameGraph        *self,
                                     double                    x,
                                     double                    y,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  sysprof_flame_graph_set_motion (self, x, y);
}

static void
sysprof_flame_graph_motion_motion_cb (SysprofFlameGraph        *self,
                                      double                    x,
                                      double                    y,
                                      GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  sysprof_flame_graph_set_motion (self, x, y);
}

static void
sysprof_flame_graph_motion_leave_cb (SysprofFlameGraph        *self,
                                     GtkEventControllerMotion *motion)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  sysprof_flame_graph_set_motion (self, 0, 0);
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

  sysprof_flame_graph_queue_scroll (self);
}

static gboolean
sysprof_flame_graph_query_tooltip (GtkWidget  *widget,
                                   int         x,
                                   int         y,
                                   gboolean    keyboard_tooltip,
                                   GtkTooltip *tooltip)
{
  SysprofFlameGraph *self = SYSPROF_FLAME_GRAPH (widget);
  FlameRectangle *rect;

  if ((rect = find_node_at_coord (self, x, y)))
    {
      g_autoptr(GString) string = g_string_new (NULL);
      SysprofSymbol *symbol = rect->node->summary->symbol;
      SysprofCallgraphNode *root = rect->node;
      SysprofCallgraphCategory category = SYSPROF_CALLGRAPH_CATEGORY_UNMASK (rect->node->category);

      while (root->parent)
        root = root->parent;

      g_string_append (string, symbol->name);

      if (symbol->binary_nick && symbol->binary_nick[0])
        g_string_append_printf (string, " [%s]", symbol->binary_nick);

      if (category && category != SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED)
        {
          const char *name = _sysprof_callgraph_category_get_name (category);

          if (name)
            g_string_append_printf (string, "\n%s: %s", _("Category"), name);
        }

      if (symbol->binary_path && symbol->binary_path[0])
        g_string_append_printf (string, "\n%s: %s", _("File"), symbol->binary_path);

      g_string_append_c (string, '\n');

      if (rect->node != root)
        g_string_append_printf (string, "%'u samples, %6.2lf%%",
                                rect->node->count,
                                rect->node->count / (double)root->count * 100.);
      else
        g_string_append_printf (string, "%'u samples", (guint)root->count);

      gtk_tooltip_set_text (tooltip, string->str);

      return TRUE;
    }

  return FALSE;
}

static void
sysprof_flame_graph_click_pressed_cb (SysprofFlameGraph *self,
                                      int                n_press,
                                      double             x,
                                      double             y,
                                      GtkGestureClick   *click)
{
  SysprofCallgraphNode *root = NULL;
  FlameRectangle *rect;

  if (n_press != 1)
    return;

  if ((rect = find_node_at_coord (self, x, y)))
    root = rect->node;
  else if (self->callgraph)
    root = &self->callgraph->root;

  if (root != self->root)
    {
      self->root = root;

      sysprof_flame_graph_invalidate (self);
    }
}

static void
sysprof_flame_graph_dispose (GObject *object)
{
  SysprofFlameGraph *self = (SysprofFlameGraph *)object;

  g_clear_handle_id (&self->queued_scroll, g_source_remove);
  g_clear_weak_pointer (&self->animation);

  g_clear_pointer (&self->rendered, gsk_render_node_unref);
  g_clear_pointer (&self->nodes, g_array_unref);
  g_clear_object (&self->callgraph);
  g_clear_object (&self->utility_traceables);

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

    case PROP_UTILITY_TRACEABLES:
      g_value_set_object (value, self->utility_traceables);
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
  widget_class->query_tooltip = sysprof_flame_graph_query_tooltip;

  properties[PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_UTILITY_TRACEABLES] =
    g_param_spec_object ("utility-traceables", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "flamegraph");
}

static void
sysprof_flame_graph_init (SysprofFlameGraph *self)
{
  GtkEventController *motion;
  GtkGesture *click;

  gtk_widget_add_css_class (GTK_WIDGET (self), "view");
  gtk_widget_set_has_tooltip (GTK_WIDGET (self), TRUE);

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

  click = gtk_gesture_click_new ();
  g_signal_connect_object (click,
                           "pressed",
                           G_CALLBACK (sysprof_flame_graph_click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));
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
          const graphene_rect_t *area,
          gboolean               recurse)
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

  if (recurse && node->children != NULL)
    {
      graphene_rect_t child_area;

      child_area.origin.x = area->origin.x;
      child_area.origin.y = area->origin.y;
      child_area.size.height = area->size.height - ROW_HEIGHT - 1;

      for (SysprofCallgraphNode *child = node->children; child; child = child->next)
        {
          double ratio = child->count / (double)node->count;
          double width;

          width = ratio * area->size.width;

          child_area.size.width = width;

          generate (array, child, &child_area, TRUE);

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

typedef struct
{
  SysprofCallgraph     *callgraph;
  SysprofCallgraphNode *root;
} Generate;

static void
generate_free (Generate *g)
{
  g_object_unref (g->callgraph);
  g->root = NULL;
  g_free (g);
}

static void
sysprof_flame_graph_generate_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  Generate *g = task_data;
  SysprofCallgraphNode **ancestors;
  g_autoptr(GArray) array = NULL;
  graphene_rect_t area;
  guint size;
  int height;

  g_assert (g!= NULL);
  g_assert (SYSPROF_IS_CALLGRAPH (g->callgraph));
  g_assert (g->root != NULL);

  size = g->root->count + g->callgraph->height;
  array = g_array_sized_new (FALSE, FALSE, sizeof (FlameRectangle), size);
  height = g->callgraph->height * ROW_HEIGHT + g->callgraph->height + 1;
  area = GRAPHENE_RECT_INIT (0, 0, G_MAXUINT16, height);

  if (g->root->parent != NULL)
    {
      guint n_ancestors = 0;
      int i;

      /* Synthesize parents so we can draw recursively but have the
       * important data copied from the parents.
       */

      for (SysprofCallgraphNode *iter = g->root->parent; iter; iter = iter->parent)
        n_ancestors++;

      ancestors = g_alloca0 (sizeof (SysprofCallgraphNode *) * n_ancestors);

      g_assert (n_ancestors > 0);
      g_assert (ancestors != NULL);

      i = n_ancestors-1;
      for (SysprofCallgraphNode *iter = g->root->parent; iter; iter = iter->parent, i--)
        ancestors[i] = iter;

      for (i = 0; i < n_ancestors; i++)
        {
          generate (array, ancestors[i], &area, FALSE);

          area.size.height -= ROW_HEIGHT;
          area.size.height -= 1;
        }
    }

  generate (array, g->root, &area, TRUE);

  gtk_tim_sort (array->data,
                array->len,
                sizeof (FlameRectangle),
                (GCompareDataFunc)sort_by_coord,
                NULL);

  g_task_return_pointer (task, g_steal_pointer (&array), (GDestroyNotify)g_array_unref);
}

static void
sysprof_flame_graph_list_traceables_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  SysprofCallgraph *callgraph = (SysprofCallgraph *)object;
  g_autoptr(SysprofFlameGraph) self = user_data;
  g_autoptr(GListModel) model = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  model = sysprof_callgraph_list_traceables_for_node_finish (callgraph, result, NULL);

  sysprof_flame_graph_set_utility_traceables (self, model);
}

static void
sysprof_flame_graph_invalidate (SysprofFlameGraph *self)
{
  g_assert (SYSPROF_IS_FLAME_GRAPH (self));

  g_clear_pointer (&self->rendered, gsk_render_node_unref);
  g_clear_pointer (&self->nodes, g_array_unref);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), NULL);

  if (self->callgraph != NULL)
    {
      g_autoptr(GTask) task = NULL;
      Generate *g;

      g = g_new0 (Generate, 1);
      g->callgraph = g_object_ref (self->callgraph);
      g->root = self->root;

      task = g_task_new (NULL, NULL, sysprof_flame_graph_generate_cb, g_object_ref (self));
      g_task_set_task_data (task, g, (GDestroyNotify)generate_free);
      g_task_run_in_thread (task, sysprof_flame_graph_generate_worker);

      if (self->root == NULL)
        sysprof_flame_graph_set_utility_traceables (self, NULL);
      else
        sysprof_callgraph_list_traceables_for_node_async (self->callgraph,
                                                          self->root,
                                                          NULL,
                                                          sysprof_flame_graph_list_traceables_cb,
                                                          g_object_ref (self));
    }
}

void
sysprof_flame_graph_set_callgraph (SysprofFlameGraph *self,
                                   SysprofCallgraph  *callgraph)
{
  g_return_if_fail (SYSPROF_IS_FLAME_GRAPH (self));
  g_return_if_fail (!callgraph || SYSPROF_IS_CALLGRAPH (callgraph));

  if (g_set_object (&self->callgraph, callgraph))
    {
      self->root = NULL;
      self->under_pointer = NULL;

      if (callgraph != NULL)
        self->root = &callgraph->root;

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CALLGRAPH]);
      gtk_widget_queue_resize (GTK_WIDGET (self));

      sysprof_flame_graph_invalidate (self);
    }
}
