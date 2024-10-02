/* sysprof-tree-expander.c
 *
 * Copyright 2022-2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-tree-expander.h"

struct _SysprofTreeExpander
{
  GtkWidget        parent_instance;

  GtkWidget       *expander;
  GtkWidget       *image;
  GtkWidget       *child;
  GtkWidget       *suffix;

  GMenuModel      *menu_model;

  GtkTreeListRow  *list_row;

  GIcon           *icon;
  GIcon           *expanded_icon;

  GtkPopover      *popover;

  gulong           list_row_notify_expanded;
};

enum {
  PROP_0,
  PROP_CHILD,
  PROP_EXPANDED,
  PROP_EXPANDED_ICON,
  PROP_EXPANDED_ICON_NAME,
  PROP_ICON,
  PROP_ICON_NAME,
  PROP_ITEM,
  PROP_LIST_ROW,
  PROP_MENU_MODEL,
  PROP_SUFFIX,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTreeExpander, sysprof_tree_expander, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_tree_expander_update_depth (SysprofTreeExpander *self)
{
  static GType builtin_icon_type = G_TYPE_INVALID;
  GtkWidget *child;
  guint depth;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));

  if (self->list_row != NULL)
    depth = gtk_tree_list_row_get_depth (self->list_row);
  else
    depth = 0;

  if G_UNLIKELY (builtin_icon_type == G_TYPE_INVALID)
    builtin_icon_type = g_type_from_name ("GtkBuiltinIcon");

  g_assert (builtin_icon_type != G_TYPE_INVALID);

  child = gtk_widget_get_prev_sibling (self->expander);

  while (child)
    {
      GtkWidget *prev = gtk_widget_get_prev_sibling (child);
      g_assert (G_TYPE_CHECK_INSTANCE_TYPE (child, builtin_icon_type));
      gtk_widget_unparent (child);
      child = prev;
    }

  for (guint i = 0; i < depth; i++)
    {
      child = g_object_new (builtin_icon_type,
                            "css-name", "indent",
                            "accessible-role", GTK_ACCESSIBLE_ROLE_PRESENTATION,
                            NULL);
      gtk_widget_insert_after (child, GTK_WIDGET (self), NULL);
    }

  /* The level property is >= 1 */
  gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                  GTK_ACCESSIBLE_PROPERTY_LEVEL, depth + 1,
                                  -1);
}

static void
sysprof_tree_expander_update_icon (SysprofTreeExpander *self)
{
  GIcon *icon = NULL;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (gtk_widget_get_parent (self->image) == GTK_WIDGET (self));

  if (self->list_row != NULL)
    {
      if (gtk_tree_list_row_get_expanded (self->list_row))
        {
          icon = self->expanded_icon ? self->expanded_icon : self->icon;
          gtk_widget_set_state_flags (self->expander, GTK_STATE_FLAG_CHECKED, FALSE);
        }
      else
        {
          icon = self->icon;
          gtk_widget_unset_state_flags (self->expander, GTK_STATE_FLAG_CHECKED);
        }

      if (gtk_tree_list_row_is_expandable (self->list_row))
        gtk_widget_add_css_class (GTK_WIDGET (self->expander), "expandable");
      else
        gtk_widget_remove_css_class (GTK_WIDGET (self->expander), "expandable");
    }

  gtk_image_set_from_gicon (GTK_IMAGE (self->image), icon);

  gtk_widget_set_visible (self->image, icon != NULL);
}

static void
sysprof_tree_expander_notify_expanded_cb (SysprofTreeExpander *self,
                                          GParamSpec          *pspec,
                                          GtkTreeListRow      *list_row)
{
  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (GTK_IS_TREE_LIST_ROW (list_row));

  sysprof_tree_expander_update_icon (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_EXPANDED]);
}

static void
sysprof_tree_expander_click_pressed_cb (SysprofTreeExpander *self,
                                        int                  n_press,
                                        double               x,
                                        double               y,
                                        GtkGestureClick     *click)
{
  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  if (n_press != 1 || self->list_row == NULL)
    return;

  gtk_widget_set_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_ACTIVE, FALSE);
}

static gboolean
expander_contains_point (SysprofTreeExpander *self,
                         double               x,
                         double               y)
{
  graphene_point_t point;

  if (!gtk_widget_compute_point (GTK_WIDGET (self), GTK_WIDGET (self->expander),
                                 &GRAPHENE_POINT_INIT (x, y), &point))
    return FALSE;

  if (point.x < 0 ||
      point.y < 0 ||
      x > gtk_widget_get_width (self->expander) ||
      y > gtk_widget_get_height (self->expander))
    return FALSE;

  return TRUE;
}

static void
sysprof_tree_expander_click_released_cb (SysprofTreeExpander *self,
                                         int                  n_press,
                                         double               x,
                                         double               y,
                                         GtkGestureClick     *click)
{
  static GType column_view_row_gtype;
  static GType list_item_widget_gtype;
  GtkWidget *row;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  if G_UNLIKELY (!column_view_row_gtype)
    column_view_row_gtype = g_type_from_name ("GtkColumnViewRowWidget");

  if G_UNLIKELY (!list_item_widget_gtype)
    list_item_widget_gtype = g_type_from_name ("GtkListItemWidget");

  gtk_widget_unset_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_ACTIVE);

  if (!(row = gtk_widget_get_ancestor (GTK_WIDGET (self), column_view_row_gtype)) &&
      !(row = gtk_widget_get_ancestor (GTK_WIDGET (self), list_item_widget_gtype)))
    return;

  gtk_widget_activate_action (row, "listitem.select", "(bb)", FALSE, FALSE);

  if (n_press == 1 &&
      gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (click)) == 3 &&
      self->menu_model != NULL)
    {
      GtkPopover *popover;
      GdkRectangle rect;

      rect.x = x;
      rect.width = 1;
      rect.height = gtk_widget_get_height (GTK_WIDGET (self));
      rect.y = 0;

      popover = g_object_new (GTK_TYPE_POPOVER_MENU,
                              "menu-model", self->menu_model,
                              "position", GTK_POS_RIGHT,
                              "pointing-to", &rect,
                              NULL);
      sysprof_tree_expander_show_popover (self, popover);
    }
  else if (n_press == 2 ||
           (n_press == 1 && expander_contains_point (self, x, y)))
    {
      gtk_widget_activate_action (GTK_WIDGET (self), "listitem.toggle-expand", NULL);
    }

  gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
sysprof_tree_expander_click_cancel_cb (SysprofTreeExpander  *self,
                                       GdkEventSequence     *sequence,
                                       GtkGestureClick      *click)
{
  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  gtk_widget_unset_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_ACTIVE);
  gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
sysprof_tree_expander_expand (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *parameter)
{
  SysprofTreeExpander *self = (SysprofTreeExpander *)widget;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));

  if (self->list_row != NULL)
    gtk_tree_list_row_set_expanded (self->list_row, TRUE);
}

static void
sysprof_tree_expander_collapse (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *parameter)
{
  SysprofTreeExpander *self = (SysprofTreeExpander *)widget;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));

  if (self->list_row != NULL)
    gtk_tree_list_row_set_expanded (self->list_row, FALSE);
}

static void
sysprof_tree_expander_toggle_expand (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *parameter)
{
  SysprofTreeExpander *self = (SysprofTreeExpander *)widget;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));

  if (self->list_row == NULL)
    return;

  gtk_tree_list_row_set_expanded (self->list_row,
                                  !gtk_tree_list_row_get_expanded (self->list_row));
}

static void
sysprof_tree_expander_dispose (GObject *object)
{
  SysprofTreeExpander *self = (SysprofTreeExpander *)object;
  GtkWidget *child;

  sysprof_tree_expander_set_list_row (self, NULL);

  g_clear_pointer (&self->expander, gtk_widget_unparent);
  g_clear_pointer (&self->image, gtk_widget_unparent);
  g_clear_pointer (&self->child, gtk_widget_unparent);
  g_clear_pointer (&self->suffix, gtk_widget_unparent);

  g_clear_object (&self->list_row);
  g_clear_object (&self->menu_model);

  g_clear_object (&self->icon);
  g_clear_object (&self->expanded_icon);

  child = gtk_widget_get_first_child (GTK_WIDGET (self));

  while (child != NULL)
    {
      GtkWidget *cur = child;
      child = gtk_widget_get_next_sibling (child);
      if (GTK_IS_POPOVER (cur))
        gtk_widget_unparent (cur);
    }

  G_OBJECT_CLASS (sysprof_tree_expander_parent_class)->dispose (object);
}

static void
sysprof_tree_expander_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofTreeExpander *self = SYSPROF_TREE_EXPANDER (object);

  switch (prop_id)
    {
    case PROP_EXPANDED:
      g_value_set_boolean (value, gtk_tree_list_row_get_expanded (self->list_row));
      break;

    case PROP_EXPANDED_ICON:
      g_value_set_object (value, sysprof_tree_expander_get_expanded_icon (self));
      break;

    case PROP_ICON:
      g_value_set_object (value, sysprof_tree_expander_get_icon (self));
      break;

    case PROP_ITEM:
      g_value_take_object (value, sysprof_tree_expander_get_item (self));
      break;

    case PROP_LIST_ROW:
      g_value_set_object (value, sysprof_tree_expander_get_list_row (self));
      break;

    case PROP_MENU_MODEL:
      g_value_set_object (value, sysprof_tree_expander_get_menu_model (self));
      break;

    case PROP_SUFFIX:
      g_value_set_object (value, sysprof_tree_expander_get_suffix (self));
      break;

    case PROP_CHILD:
      g_value_set_object (value, sysprof_tree_expander_get_child (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tree_expander_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofTreeExpander *self = SYSPROF_TREE_EXPANDER (object);

  switch (prop_id)
    {
    case PROP_EXPANDED_ICON:
      sysprof_tree_expander_set_expanded_icon (self, g_value_get_object (value));
      break;

    case PROP_EXPANDED_ICON_NAME:
      sysprof_tree_expander_set_expanded_icon_name (self, g_value_get_string (value));
      break;

    case PROP_ICON:
      sysprof_tree_expander_set_icon (self, g_value_get_object (value));
      break;

    case PROP_ICON_NAME:
      sysprof_tree_expander_set_icon_name (self, g_value_get_string (value));
      break;

    case PROP_LIST_ROW:
      sysprof_tree_expander_set_list_row (self, g_value_get_object (value));
      break;

    case PROP_MENU_MODEL:
      sysprof_tree_expander_set_menu_model (self, g_value_get_object (value));
      break;

    case PROP_SUFFIX:
      sysprof_tree_expander_set_suffix (self, g_value_get_object (value));
      break;

    case PROP_CHILD:
      sysprof_tree_expander_set_child (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tree_expander_class_init (SysprofTreeExpanderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_tree_expander_dispose;
  object_class->get_property = sysprof_tree_expander_get_property;
  object_class->set_property = sysprof_tree_expander_set_property;

  properties [PROP_EXPANDED] =
    g_param_spec_boolean ("expanded", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_EXPANDED_ICON] =
    g_param_spec_object ("expanded-icon", NULL, NULL,
                         G_TYPE_ICON,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_EXPANDED_ICON_NAME] =
    g_param_spec_string ("expanded-icon-name", NULL, NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ICON] =
    g_param_spec_object ("icon", NULL, NULL,
                         G_TYPE_ICON,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", NULL, NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_LIST_ROW] =
    g_param_spec_object ("list-row", NULL, NULL,
                         GTK_TYPE_TREE_LIST_ROW,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model", NULL, NULL,
                         G_TYPE_MENU_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SUFFIX] =
    g_param_spec_object ("suffix", NULL, NULL,
                         GTK_TYPE_WIDGET,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CHILD] =
    g_param_spec_object ("child", NULL, NULL,
                         GTK_TYPE_WIDGET,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "treeexpander");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GROUP);

  gtk_widget_class_install_action (widget_class, "listitem.toggle-expand", NULL, sysprof_tree_expander_toggle_expand);
  gtk_widget_class_install_action (widget_class, "listitem.collapse", NULL, sysprof_tree_expander_collapse);
  gtk_widget_class_install_action (widget_class, "listitem.expand", NULL, sysprof_tree_expander_expand);
}

static void
sysprof_tree_expander_init (SysprofTreeExpander *self)
{
  GtkEventController *controller;

  self->expander = g_object_new (g_type_from_name ("GtkBuiltinIcon"),
                                 "css-name", "expander",
                                 NULL);
  gtk_widget_insert_after (self->expander, GTK_WIDGET (self), NULL);

  self->image = g_object_new (GTK_TYPE_IMAGE,
                              "visible", FALSE,
                              NULL);
  gtk_widget_insert_after (self->image, GTK_WIDGET (self), self->expander);

  controller = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (controller), 0);
  g_signal_connect_object (controller,
                           "pressed",
                           G_CALLBACK (sysprof_tree_expander_click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (controller,
                           "released",
                           G_CALLBACK (sysprof_tree_expander_click_released_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (controller,
                           "cancel",
                           G_CALLBACK (sysprof_tree_expander_click_cancel_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);
}

GtkWidget *
sysprof_tree_expander_new (void)
{
  return g_object_new (SYSPROF_TYPE_TREE_EXPANDER, NULL);
}

/**
 * sysprof_tree_expander_get_item:
 * @self: a #SysprofTreeExpander
 *
 * Gets the item instance from the model.
 *
 * Returns: (transfer full) (nullable) (type GObject): a #GObject or %NULL
 */
gpointer
sysprof_tree_expander_get_item (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  if (self->list_row == NULL)
    return NULL;

  return gtk_tree_list_row_get_item (self->list_row);
}

/**
 * sysprof_tree_expander_get_menu_model:
 * @self: a #SysprofTreeExpander
 *
 * Sets the menu model to use for context menus.
 *
 * Returns: (transfer none) (nullable): a #GMenuModel or %NULL
 */
GMenuModel *
sysprof_tree_expander_get_menu_model (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->menu_model;
}

void
sysprof_tree_expander_set_menu_model (SysprofTreeExpander *self,
                                      GMenuModel          *menu_model)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (!menu_model || G_IS_MENU_MODEL (menu_model));

  if (g_set_object (&self->menu_model, menu_model))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MENU_MODEL]);
}

/**
 * sysprof_tree_expander_get_icon:
 * @self: a #SysprofTreeExpander
 *
 * Gets the icon for the row.
 *
 * Returns: (transfer none) (nullable): a #GIcon or %NULL
 */
GIcon *
sysprof_tree_expander_get_icon (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->icon;
}

/**
 * sysprof_tree_expander_get_expanded_icon:
 * @self: a #SysprofTreeExpander
 *
 * Gets the icon for the row when expanded.
 *
 * Returns: (transfer none) (nullable): a #GIcon or %NULL
 */
GIcon *
sysprof_tree_expander_get_expanded_icon (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->expanded_icon;
}

void
sysprof_tree_expander_set_icon (SysprofTreeExpander *self,
                                GIcon               *icon)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (!icon || G_IS_ICON (icon));

  if (g_set_object (&self->icon, icon))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ICON]);
      sysprof_tree_expander_update_icon (self);
    }
}

void
sysprof_tree_expander_set_expanded_icon (SysprofTreeExpander *self,
                                         GIcon               *expanded_icon)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (!expanded_icon || G_IS_ICON (expanded_icon));

  if (g_set_object (&self->expanded_icon, expanded_icon))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_EXPANDED_ICON]);
      sysprof_tree_expander_update_icon (self);
    }
}

void
sysprof_tree_expander_set_expanded_icon_name (SysprofTreeExpander *self,
                                              const char          *expanded_icon_name)
{
  g_autoptr(GIcon) expanded_icon = NULL;

  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));

  if (expanded_icon_name != NULL)
    expanded_icon = g_themed_icon_new (expanded_icon_name);

  sysprof_tree_expander_set_expanded_icon (self, expanded_icon);
}

void
sysprof_tree_expander_set_icon_name (SysprofTreeExpander *self,
                                     const char          *icon_name)
{
  g_autoptr(GIcon) icon = NULL;

  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));

  if (icon_name != NULL)
    icon = g_themed_icon_new (icon_name);

  sysprof_tree_expander_set_icon (self, icon);
}

/**
 * sysprof_tree_expander_get_suffix:
 * @self: a #SysprofTreeExpander
 *
 * Get the suffix widget, if any.
 *
 * Returns: (transfer none) (nullable): a #GtkWidget
 */
GtkWidget *
sysprof_tree_expander_get_suffix (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->suffix;
}

void
sysprof_tree_expander_set_suffix (SysprofTreeExpander *self,
                                  GtkWidget           *suffix)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (!suffix || GTK_IS_WIDGET (suffix));

  if (self->suffix == suffix)
    return;

  g_clear_pointer (&self->suffix, gtk_widget_unparent);

  self->suffix = suffix;

  if (self->suffix)
    gtk_widget_insert_before (suffix, GTK_WIDGET (self), NULL);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUFFIX]);
}

GtkWidget *
sysprof_tree_expander_get_child (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->child;
}

void
sysprof_tree_expander_set_child (SysprofTreeExpander *self,
                                 GtkWidget           *child)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));

  if (child == self->child)
    return;

  if (child)
    g_object_ref (child);

  g_clear_pointer (&self->child, gtk_widget_unparent);

  self->child = child;

  if (child)
    gtk_widget_insert_after (child, GTK_WIDGET (self), self->image);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHILD]);
}

/**
 * sysprof_tree_expander_get_list_row:
 * @self: a #SysprofTreeExpander
 *
 * Gets the list row for the expander.
 *
 * Returns: (transfer none) (nullable): a #GtkTreeListRow or %NULL
 */
GtkTreeListRow *
sysprof_tree_expander_get_list_row (SysprofTreeExpander *self)
{
  g_return_val_if_fail (SYSPROF_IS_TREE_EXPANDER (self), NULL);

  return self->list_row;
}

static void
sysprof_tree_expander_clear_list_row (SysprofTreeExpander *self)
{
  GtkWidget *child;

  g_assert (SYSPROF_IS_TREE_EXPANDER (self));

  if (self->list_row == NULL)
    return;

  g_clear_signal_handler (&self->list_row_notify_expanded, self->list_row);

  g_clear_object (&self->list_row);

  gtk_image_set_from_icon_name (GTK_IMAGE (self->image), NULL);

  child = gtk_widget_get_prev_sibling (self->expander);

  while (child)
    {
      GtkWidget *prev = gtk_widget_get_prev_sibling (child);
      gtk_widget_unparent (child);
      child = prev;
    }
}

void
sysprof_tree_expander_set_list_row (SysprofTreeExpander *self,
                                    GtkTreeListRow      *list_row)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (!list_row || GTK_IS_TREE_LIST_ROW (list_row));

  if (self->list_row == list_row)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  sysprof_tree_expander_clear_list_row (self);

  if (list_row != NULL)
    {
      self->list_row = g_object_ref (list_row);
      self->list_row_notify_expanded =
        g_signal_connect_object (self->list_row,
                                 "notify::expanded",
                                 G_CALLBACK (sysprof_tree_expander_notify_expanded_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
      sysprof_tree_expander_update_depth (self);
      sysprof_tree_expander_update_icon (self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIST_ROW]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPANDED]);

  g_object_thaw_notify (G_OBJECT (self));
}

static gboolean
sysprof_tree_expander_remove_popover_idle (gpointer user_data)
{
  gpointer *pair = user_data;

  g_assert (pair != NULL);
  g_assert (SYSPROF_IS_TREE_EXPANDER (pair[0]));
  g_assert (GTK_IS_POPOVER (pair[1]));

  if (gtk_widget_get_parent (pair[1]) == pair[0])
    gtk_widget_unparent (pair[1]);

  g_object_unref (pair[0]);
  g_object_unref (pair[1]);
  g_free (pair);

  return G_SOURCE_REMOVE;
}

static void
sysprof_tree_expander_popover_closed_cb (SysprofTreeExpander *self,
                                         GtkPopover          *popover)
{
  g_assert (SYSPROF_IS_TREE_EXPANDER (self));
  g_assert (GTK_IS_POPOVER (popover));

  if (self->popover == popover)
    {
      gpointer *pair = g_new (gpointer, 2);
      pair[0] = g_object_ref (self);
      pair[1] = g_object_ref (popover);
      /* We don't want to unparent the widget immediately because it gets
       * closed _BEFORE_ executing GAction. So removing it will cause the
       * actions to be unavailable.
       *
       * Instead, defer to an idle where we remove the popover.
       */
      g_idle_add (sysprof_tree_expander_remove_popover_idle, pair);
      self->popover = NULL;
    }
}

void
sysprof_tree_expander_show_popover (SysprofTreeExpander *self,
                                    GtkPopover          *popover)
{
  g_return_if_fail (SYSPROF_IS_TREE_EXPANDER (self));
  g_return_if_fail (GTK_IS_POPOVER (popover));

  gtk_widget_set_parent (GTK_WIDGET (popover), GTK_WIDGET (self));

  g_signal_connect_object (popover,
                           "closed",
                           G_CALLBACK (sysprof_tree_expander_popover_closed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  if (self->popover != NULL)
    gtk_popover_popdown (self->popover);

  self->popover = popover;

  gtk_popover_popup (popover);
}
