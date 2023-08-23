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

  object_class->dispose = sysprof_flame_graph_dispose;
  object_class->get_property = sysprof_flame_graph_get_property;
  object_class->set_property = sysprof_flame_graph_set_property;

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
