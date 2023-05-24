/* sysprof-callgraph-frame.c
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

#include "sysprof-callgraph-frame.h"

struct _SysprofCallgraphFrame
{
  GObject parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCallgraphFrame, sysprof_callgraph_frame, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_callgraph_frame_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_callgraph_frame_parent_class)->finalize (object);
}

static void
sysprof_callgraph_frame_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_frame_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_frame_class_init (SysprofCallgraphFrameClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_callgraph_frame_finalize;
  object_class->get_property = sysprof_callgraph_frame_get_property;
  object_class->set_property = sysprof_callgraph_frame_set_property;
}

static void
sysprof_callgraph_frame_init (SysprofCallgraphFrame *self)
{
}
