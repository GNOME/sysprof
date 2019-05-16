/* sysprof-empty-state-view.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-empty-state-view"

#include "config.h"

#include <string.h>

#include "sysprof-empty-state-view.h"

typedef struct
{
  GtkImage *image;
  GtkLabel *title;
  GtkLabel *subtitle;
} SysprofEmptyStateViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofEmptyStateView, sysprof_empty_state_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_TITLE,
  PROP_SUBTITLE,
  PROP_ICON_NAME,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sysprof_empty_state_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_EMPTY_STATE_VIEW, NULL);
}

static gboolean
sysprof_empty_state_view_action (GtkWidget   *widget,
                                 const gchar *prefix,
                                 const gchar *action_name,
                                 GVariant    *parameter)
{
  GtkWidget *toplevel;
  GApplication *app;
  GActionGroup *group = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (prefix, FALSE);
  g_return_val_if_fail (action_name, FALSE);

  app = g_application_get_default ();
  toplevel = gtk_widget_get_toplevel (widget);

  while ((group == NULL) && (widget != NULL))
    {
      group = gtk_widget_get_action_group (widget, prefix);
      widget = gtk_widget_get_parent (widget);
    }

  if (!group && g_str_equal (prefix, "win") && G_IS_ACTION_GROUP (toplevel))
    group = G_ACTION_GROUP (toplevel);

  if (!group && g_str_equal (prefix, "app") && G_IS_ACTION_GROUP (app))
    group = G_ACTION_GROUP (app);

  if (group && g_action_group_has_action (group, action_name))
    {
      g_action_group_activate_action (group, action_name, parameter);
      return TRUE;
    }

  if (parameter && g_variant_is_floating (parameter))
    {
      parameter = g_variant_ref_sink (parameter);
      g_variant_unref (parameter);
    }

  g_warning ("Failed to locate action %s.%s", prefix, action_name);

  return FALSE;
}

static gboolean
sysprof_empty_state_view_activate_link (SysprofEmptyStateView *self,
                                        const gchar           *uri,
                                        GtkLabel              *label)
{
  g_assert (SYSPROF_IS_EMPTY_STATE_VIEW (self));
  g_assert (uri != NULL);
  g_assert (GTK_IS_LABEL (label));

  if (g_str_has_prefix (uri, "action://"))
    {
      g_autofree gchar *full_name = NULL;
      g_autofree gchar *action_name = NULL;
      g_autofree gchar *group_name = NULL;
      g_autoptr(GVariant) param = NULL;
      g_autoptr(GError) error = NULL;

      uri += strlen ("action://");

      if (g_action_parse_detailed_name (uri, &full_name, &param, &error))
        {
          const gchar *dot = strchr (full_name, '.');

          if (param != NULL && g_variant_is_floating (param))
            param = g_variant_ref_sink (param);

          if (dot == NULL)
            return FALSE;

          group_name = g_strndup (full_name, dot - full_name);
          action_name = g_strdup (++dot);

          sysprof_empty_state_view_action (GTK_WIDGET (self),
                                      group_name,
                                      action_name,
                                      param);

          return TRUE;
        }
      else
        g_warning ("%s", error->message);
    }

  return FALSE;
}

static void
sysprof_empty_state_view_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SysprofEmptyStateView *self = SYSPROF_EMPTY_STATE_VIEW (object);
  SysprofEmptyStateViewPrivate *priv = sysprof_empty_state_view_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ICON_NAME:
      g_object_set (priv->image,
                    "icon-name", g_value_get_string (value),
                    NULL);
      break;

    case PROP_TITLE:
      gtk_label_set_label (priv->title, g_value_get_string (value));
      break;

    case PROP_SUBTITLE:
      gtk_label_set_label (priv->subtitle, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_empty_state_view_class_init (SysprofEmptyStateViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = sysprof_empty_state_view_set_property;

  properties [PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", NULL, NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBTITLE] =
    g_param_spec_string ("subtitle", NULL, NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-empty-state-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofEmptyStateView, subtitle);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofEmptyStateView, title);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofEmptyStateView, image);
}

static void
sysprof_empty_state_view_init (SysprofEmptyStateView *self)
{
  SysprofEmptyStateViewPrivate *priv = sysprof_empty_state_view_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (priv->subtitle,
                           "activate-link",
                           G_CALLBACK (sysprof_empty_state_view_activate_link),
                           self,
                           G_CONNECT_SWAPPED);
}
