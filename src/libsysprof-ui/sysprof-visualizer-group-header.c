/* sysprof-visualizer-group-header.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-visualizer-group-header"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-visualizer.h"
#include "sysprof-visualizer-group.h"
#include "sysprof-visualizer-group-header.h"
#include "sysprof-visualizer-group-private.h"

struct _SysprofVisualizerGroupHeader
{
  GtkListBoxRow parent_instance;

  SysprofVisualizerGroup *group;
  GtkBox *box;
};

G_DEFINE_TYPE (SysprofVisualizerGroupHeader, sysprof_visualizer_group_header, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_GROUP,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_visualizer_group_header_dispose (GObject *object)
{
  SysprofVisualizerGroupHeader *self = (SysprofVisualizerGroupHeader *)object;

  if (self->box)
    {
      gtk_widget_unparent (GTK_WIDGET (self->box));
      self->box = NULL;
    }

  G_OBJECT_CLASS (sysprof_visualizer_group_header_parent_class)->dispose (object);
}

static void
sysprof_visualizer_group_header_get_property (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
  SysprofVisualizerGroupHeader *self = SYSPROF_VISUALIZER_GROUP_HEADER (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, _sysprof_visualizer_group_header_get_group (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_group_header_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
  SysprofVisualizerGroupHeader *self = SYSPROF_VISUALIZER_GROUP_HEADER (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      self->group = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_group_header_class_init (SysprofVisualizerGroupHeaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_visualizer_group_header_dispose;
  object_class->get_property = sysprof_visualizer_group_header_get_property;
  object_class->set_property = sysprof_visualizer_group_header_set_property;

  properties [PROP_GROUP] =
    g_param_spec_object ("group",
                         "Group",
                         "The group",
                         SYSPROF_TYPE_VISUALIZER_GROUP,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_visualizer_group_header_init (SysprofVisualizerGroupHeader *self)
{
  self->box = g_object_new (GTK_TYPE_BOX,
                            "orientation", GTK_ORIENTATION_VERTICAL,
                            "visible", TRUE,
                            NULL);
  gtk_widget_set_parent (GTK_WIDGET (self->box), GTK_WIDGET (self));
}

void
_sysprof_visualizer_group_header_add_row (SysprofVisualizerGroupHeader *self,
                                          guint                         position,
                                          const gchar                  *title,
                                          GMenuModel                   *menu,
                                          GtkWidget                    *widget)
{
  GtkWidget *sibling;
  GtkBox *box;

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP_HEADER (self));
  g_return_if_fail (SYSPROF_IS_VISUALIZER (widget));
  g_return_if_fail (!menu || G_IS_MENU_MODEL (menu));

  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                      "spacing", 6,
                      "visible", TRUE,
                      NULL);
  g_object_bind_property (widget, "visible", box, "visible", G_BINDING_SYNC_CREATE);

  sibling = gtk_widget_get_first_child (GTK_WIDGET (self->box));
  for (; position > 1 && sibling; position--)
    sibling = gtk_widget_get_next_sibling (sibling);
  gtk_box_insert_child_after (self->box, GTK_WIDGET (box), sibling);

  if (title != NULL)
    {
      g_autoptr(GtkSizeGroup) size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
      PangoAttrList *attrs = pango_attr_list_new ();
      GtkLabel *label;

      pango_attr_list_insert (attrs, pango_attr_scale_new (0.83333));
      label = g_object_new (GTK_TYPE_LABEL,
                            "attributes", attrs,
                            "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                            "margin-top", 6,
                            "margin-bottom", 6,
                            "margin-start", 6,
                            "margin-end", 6,
                            "hexpand", TRUE,
                            "label", title,
                            "visible", TRUE,
                            "xalign", 0.0f,
                            NULL);
      gtk_box_append (box, GTK_WIDGET (label));
      pango_attr_list_unref (attrs);

      gtk_size_group_add_widget (size_group, widget);
      gtk_size_group_add_widget (size_group, GTK_WIDGET (box));
    }

  if (position == 0 && sysprof_visualizer_group_get_has_page (self->group))
    {
      GtkImage *image;

      image = g_object_new (GTK_TYPE_IMAGE,
                            "icon-name", "view-paged-symbolic",
                            "tooltip-text", _("Select for more details"),
                            "pixel-size", 16,
                            "visible", TRUE,
                            NULL);
      gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (image)), "dim-label");
      gtk_box_append (box, GTK_WIDGET (image));
    }

  if (menu != NULL)
    {
      GtkStyleContext *style_context;
      GtkMenuButton *button;

      button = g_object_new (GTK_TYPE_MENU_BUTTON,
                             "child", g_object_new (GTK_TYPE_IMAGE,
                                                    "icon-name", "view-more-symbolic",
                                                    "visible", TRUE,
                                                    NULL),
                             "margin-end", 6,
                             "direction", GTK_ARROW_RIGHT,
                             "halign", GTK_ALIGN_CENTER,
                             "menu-model", menu,
                             "tooltip-text", _("Display supplemental graphs"),
                             "valign", GTK_ALIGN_CENTER,
                             "visible", TRUE,
                             NULL);
      style_context = gtk_widget_get_style_context (GTK_WIDGET (button));
      gtk_style_context_add_class (style_context, "image-button");
      gtk_style_context_add_class (style_context, "small-button");
      gtk_style_context_add_class (style_context, "flat");

      gtk_box_append (box, GTK_WIDGET (button));
    }
}

SysprofVisualizerGroupHeader *
_sysprof_visualizer_group_header_new (SysprofVisualizerGroup *group)
{
  return g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP_HEADER,
                       "group", group,
                       NULL);
}

SysprofVisualizerGroup *
_sysprof_visualizer_group_header_get_group (SysprofVisualizerGroupHeader *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_GROUP_HEADER(self), NULL);

  return self->group;
}
