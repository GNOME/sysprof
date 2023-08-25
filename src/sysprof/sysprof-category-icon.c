/* sysprof-category-icon.c
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

#include "sysprof-category-icon.h"

struct _SysprofCategoryIcon
{
  GtkWidget parent_instance;
  SysprofCallgraphCategory category;
};

enum {
  PROP_0,
  PROP_CATEGORY,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCategoryIcon, sysprof_category_icon, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];
static GdkRGBA category_colors[SYSPROF_CALLGRAPH_CATEGORY_LAST];

static void
sysprof_category_icon_snapshot (GtkWidget   *widget,
                                GtkSnapshot *snapshot)
{
  SysprofCategoryIcon *self = (SysprofCategoryIcon *)widget;

  g_assert (SYSPROF_IS_CATEGORY_ICON (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->category >= G_N_ELEMENTS (category_colors))
    return;

  if (category_colors[self->category].alpha == 0)
    return;

  gtk_snapshot_append_color (snapshot,
                             &category_colors[self->category],
                             &GRAPHENE_RECT_INIT (0, 0,
                                                  gtk_widget_get_width (widget),
                                                  gtk_widget_get_height (widget)));
}

static void
sysprof_category_icon_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofCategoryIcon *self = SYSPROF_CATEGORY_ICON (object);

  switch (prop_id)
    {
    case PROP_CATEGORY:
      g_value_set_enum (value, sysprof_category_icon_get_category (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_icon_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofCategoryIcon *self = SYSPROF_CATEGORY_ICON (object);

  switch (prop_id)
    {
      case PROP_CATEGORY:
      sysprof_category_icon_set_category (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_icon_class_init (SysprofCategoryIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = sysprof_category_icon_get_property;
  object_class->set_property = sysprof_category_icon_set_property;

  widget_class->snapshot = sysprof_category_icon_snapshot;

  properties[PROP_CATEGORY] =
    g_param_spec_enum ("category", NULL, NULL,
                       SYSPROF_TYPE_CALLGRAPH_CATEGORY,
                       SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED,
                       (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_A11Y], "#000");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_ACTIONS], "#f66151");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_CONTEXT_SWITCH], "#ffbe6f");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_COREDUMP], "#ff0000");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_CSS], "#62a0ea");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_GRAPHICS], "#ed333b");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_ICONS], "#613583");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_INPUT], "#1a5fb4");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_IO], "#cdab8f");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_IPC], "#e5a50a");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_JAVASCRIPT], "#1c71d8");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_KERNEL], "#a51d2d");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_LAYOUT], "#9141ac");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_LOCKING], "#f5c211");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP], "#5e5c64");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_MEMORY], "#f9f06b");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_PAINT], "#2ec27e");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_TYPE_SYSTEM], "#241f31");
  gdk_rgba_parse (&category_colors[SYSPROF_CALLGRAPH_CATEGORY_WINDOWING], "#c64600");
}

static void
sysprof_category_icon_init (SysprofCategoryIcon *self)
{
  self->category = SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;
}

SysprofCallgraphCategory
sysprof_category_icon_get_category (SysprofCategoryIcon *self)
{
  g_return_val_if_fail (SYSPROF_IS_CATEGORY_ICON (self), 0);

  return self->category;
}

void
sysprof_category_icon_set_category (SysprofCategoryIcon      *self,
                                    SysprofCallgraphCategory  category)
{
  g_return_if_fail (SYSPROF_IS_CATEGORY_ICON (self));
  g_return_if_fail (category > 0);
  g_return_if_fail (category < SYSPROF_CALLGRAPH_CATEGORY_LAST);

  if (self->category == category)
    return;

  self->category = category;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

const GdkRGBA *
sysprof_callgraph_category_get_color (SysprofCallgraphCategory category)
{
  if (category < G_N_ELEMENTS (category_colors) && category_colors[category].alpha > 0)
    return &category_colors[category];

  return NULL;
}
