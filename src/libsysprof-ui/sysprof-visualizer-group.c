/* sysprof-visualizer-group.c
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

#define G_LOG_DOMAIN "sysprof-visualizer-group"

#include "config.h"

#include <dazzle.h>
#include <glib/gi18n.h>

#include "sysprof-visualizer.h"
#include "sysprof-visualizer-group.h"
#include "sysprof-visualizer-group-private.h"

typedef struct
{
  /* Owned pointers */
  GMenuModel                   *menu;
  GMenu                        *default_menu;
  GMenu                        *rows_menu;
  gchar                        *title;
  GtkSizeGroup                 *size_group;
  GSimpleActionGroup           *actions;

  gint priority;

  guint has_page : 1;

  /* Weak pointers */
  SysprofVisualizerGroupHeader *header;

  /* Child Widgets */
  GtkBox                       *visualizers;
} SysprofVisualizerGroupPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofVisualizerGroup, sysprof_visualizer_group, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_HAS_PAGE,
  PROP_MENU,
  PROP_PRIORITY,
  PROP_TITLE,
  N_PROPS
};

enum {
  GROUP_ACTIVATED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

/**
 * sysprof_visualizer_group_new:
 *
 * Create a new #SysprofVisualizerGroup.
 *
 * Returns: (transfer full): a newly created #SysprofVisualizerGroup
 */
SysprofVisualizerGroup *
sysprof_visualizer_group_new (void)
{
  return g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP, NULL);
}

const gchar *
sysprof_visualizer_group_get_title (SysprofVisualizerGroup *self)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self), NULL);

  return priv->title;
}

void
sysprof_visualizer_group_set_title (SysprofVisualizerGroup *self,
                                    const gchar            *title)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));

  if (g_strcmp0 (priv->title, title) != 0)
    {
      g_free (priv->title);
      priv->title = g_strdup (title);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
    }
}

/**
 * sysprof_visualizer_group_get_menu:
 *
 * Gets the menu for the group.
 *
 * Returns: (transfer none) (nullable): a #GMenuModel or %NULL
 *
 * Since: 3.34
 */
GMenuModel *
sysprof_visualizer_group_get_menu (SysprofVisualizerGroup *self)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self), NULL);

  return priv->menu;
}

void
sysprof_visualizer_group_set_menu (SysprofVisualizerGroup *self,
                                   GMenuModel             *menu)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));
  g_return_if_fail (!menu || G_IS_MENU_MODEL (menu));

  if (g_set_object (&priv->menu, menu))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MENU]);
}

static gchar *
create_action_name (const gchar *str)
{
  GString *ret = g_string_new (NULL);

  for (; *str; str = g_utf8_next_char (str))
    {
      gunichar ch = g_utf8_get_char (str);

      if (g_unichar_isalnum (ch))
        g_string_append_unichar (ret, ch);
      else
        g_string_append_c (ret, '_');
    }

  return g_string_free (ret, FALSE);
}

static void
sysprof_visualizer_group_add (GtkContainer *container,
                              GtkWidget    *child)
{
  SysprofVisualizerGroup *self = (SysprofVisualizerGroup *)container;

  g_assert (SYSPROF_IS_VISUALIZER_GROUP (self));
  g_assert (GTK_IS_WIDGET (child));

  if (SYSPROF_IS_VISUALIZER (child))
    sysprof_visualizer_group_insert (self, SYSPROF_VISUALIZER (child), -1, FALSE);
  else
    GTK_CONTAINER_CLASS (sysprof_visualizer_group_parent_class)->add (container, child);
}

static void
sysprof_visualizer_group_finalize (GObject *object)
{
  SysprofVisualizerGroup *self = (SysprofVisualizerGroup *)object;
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_clear_pointer (&priv->title, g_free);
  g_clear_object (&priv->menu);
  g_clear_object (&priv->size_group);
  g_clear_object (&priv->default_menu);
  g_clear_object (&priv->rows_menu);
  g_clear_object (&priv->actions);

  dzl_clear_weak_pointer (&priv->header);

  G_OBJECT_CLASS (sysprof_visualizer_group_parent_class)->finalize (object);
}

static void
sysprof_visualizer_group_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofVisualizerGroup *self = SYSPROF_VISUALIZER_GROUP (object);

  switch (prop_id)
    {
    case PROP_HAS_PAGE:
      g_value_set_boolean (value, sysprof_visualizer_group_get_has_page (self));
      break;

    case PROP_MENU:
      g_value_set_object (value, sysprof_visualizer_group_get_menu (self));
      break;

    case PROP_PRIORITY:
      g_value_set_int (value, sysprof_visualizer_group_get_priority (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_visualizer_group_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_group_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SysprofVisualizerGroup *self = SYSPROF_VISUALIZER_GROUP (object);

  switch (prop_id)
    {
    case PROP_HAS_PAGE:
      sysprof_visualizer_group_set_has_page (self, g_value_get_boolean (value));
      break;

    case PROP_MENU:
      sysprof_visualizer_group_set_menu (self, g_value_get_object (value));
      break;

    case PROP_PRIORITY:
      sysprof_visualizer_group_set_priority (self, g_value_get_int (value));
      break;

    case PROP_TITLE:
      sysprof_visualizer_group_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_group_class_init (SysprofVisualizerGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->finalize = sysprof_visualizer_group_finalize;
  object_class->get_property = sysprof_visualizer_group_get_property;
  object_class->set_property = sysprof_visualizer_group_set_property;

  container_class->add = sysprof_visualizer_group_add;

  properties [PROP_HAS_PAGE] =
    g_param_spec_boolean ("has-page",
                          "Has Page",
                          "Has Page",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_MENU] =
    g_param_spec_object ("menu",
                         "Menu",
                         "Menu",
                         G_TYPE_MENU_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_PRIORITY] =
    g_param_spec_int ("priority",
                      "Priority",
                      "The Priority of the group, used for sorting",
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [GROUP_ACTIVATED] =
    g_signal_new ("group-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_css_name (widget_class, "SysprofVisualizerGroup");
}

static void
sysprof_visualizer_group_init (SysprofVisualizerGroup *self)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);
  g_autoptr(GMenuItem) item = NULL;

  priv->actions = g_simple_action_group_new ();

  priv->default_menu = g_menu_new ();
  priv->rows_menu = g_menu_new ();

  item = g_menu_item_new_section (NULL, G_MENU_MODEL (priv->rows_menu));
  g_menu_append_item (priv->default_menu, item);

  priv->menu = g_object_ref (G_MENU_MODEL (priv->default_menu));

  priv->size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
  gtk_size_group_add_widget (priv->size_group, GTK_WIDGET (self));

  priv->visualizers = g_object_new (GTK_TYPE_BOX,
                                    "orientation", GTK_ORIENTATION_VERTICAL,
                                    "visible", TRUE,
                                    NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (priv->visualizers));
}

void
_sysprof_visualizer_group_set_header (SysprofVisualizerGroup       *self,
                                      SysprofVisualizerGroupHeader *header)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));
  g_return_if_fail (!header || SYSPROF_IS_VISUALIZER_GROUP_HEADER (header));

  if (dzl_set_weak_pointer (&priv->header, header))
    {
      if (header != NULL)
        {
          GList *children;
          guint position = 0;

          gtk_widget_insert_action_group (GTK_WIDGET (header),
                                          "group",
                                          G_ACTION_GROUP (priv->actions));
          gtk_size_group_add_widget (priv->size_group, GTK_WIDGET (header));

          children = gtk_container_get_children (GTK_CONTAINER (priv->visualizers));

          for (const GList *iter = children; iter; iter = iter->next)
            {
              SysprofVisualizer *vis = iter->data;
              const gchar *title;
              GMenuModel *menu = NULL;

              g_assert (SYSPROF_IS_VISUALIZER (vis));

              if (position == 0)
                menu = priv->menu;

              title = sysprof_visualizer_get_title (vis);

              if (title == NULL)
                title = priv->title;

              _sysprof_visualizer_group_header_add_row (header,
                                                        position,
                                                        title,
                                                        menu,
                                                        GTK_WIDGET (vis));

              position++;
            }

          g_list_free (children);
        }
    }
}

static void
sysprof_visualizer_group_set_reader_cb (SysprofVisualizer    *visualizer,
                                        SysprofCaptureReader *reader)
{
  sysprof_visualizer_set_reader (visualizer, reader);
}

void
_sysprof_visualizer_group_set_reader (SysprofVisualizerGroup *self,
                                      SysprofCaptureReader   *reader)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));
  g_return_if_fail (reader != NULL);

  gtk_container_foreach (GTK_CONTAINER (priv->visualizers),
                         (GtkCallback) sysprof_visualizer_group_set_reader_cb,
                         reader);
}

void
sysprof_visualizer_group_insert (SysprofVisualizerGroup *self,
                                 SysprofVisualizer      *visualizer,
                                 gint                    position,
                                 gboolean                can_toggle)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));
  g_return_if_fail (SYSPROF_IS_VISUALIZER (visualizer));

  gtk_container_add_with_properties (GTK_CONTAINER (priv->visualizers), GTK_WIDGET (visualizer),
                                     "position", position,
                                     NULL);

  if (can_toggle)
    {
      const gchar *title = sysprof_visualizer_get_title (visualizer);
      g_autofree gchar *action_name = create_action_name (title);
      g_autofree gchar *full_action_name = g_strdup_printf ("group.%s", action_name);
      g_autoptr(GMenuItem) item = g_menu_item_new (title, full_action_name);
      g_autoptr(GPropertyAction) action = NULL;

      action = g_property_action_new (action_name, visualizer, "visible");
      g_action_map_add_action (G_ACTION_MAP (priv->actions), G_ACTION (action));
      g_menu_item_set_attribute (item, "role", "s", "check");
      g_menu_append_item (priv->rows_menu, item);
    }
}

gint
sysprof_visualizer_group_get_priority (SysprofVisualizerGroup *self)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self), 0);

  return priv->priority;
}

void
sysprof_visualizer_group_set_priority (SysprofVisualizerGroup *self,
                                       gint                    priority)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));

  if (priv->priority != priority)
    {
      priv->priority = priority;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PRIORITY]);
    }
}

gboolean
sysprof_visualizer_group_get_has_page (SysprofVisualizerGroup *self)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self), FALSE);

  return priv->has_page;
}

void
sysprof_visualizer_group_set_has_page (SysprofVisualizerGroup *self,
                                       gboolean                has_page)
{
  SysprofVisualizerGroupPrivate *priv = sysprof_visualizer_group_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER_GROUP (self));

  has_page = !!has_page;

  if (has_page != priv->has_page)
    {
      priv->has_page = has_page;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_PAGE]);
    }
}
