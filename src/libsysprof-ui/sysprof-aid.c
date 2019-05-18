/* sysprof-aid.c
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

#define G_LOG_DOMAIN "sysprof-aid"

#include "config.h"

#include "sysprof-aid.h"

typedef struct
{
  gchar *display_name;
  GIcon *icon;
} SysprofAidPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SysprofAid, sysprof_aid, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_DISPLAY_NAME,
  PROP_ICON,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_aid_finalize (GObject *object)
{
  SysprofAid *self = (SysprofAid *)object;
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_clear_pointer (&priv->display_name, g_free);
  g_clear_object (&priv->icon);

  G_OBJECT_CLASS (sysprof_aid_parent_class)->finalize (object);
}

static void
sysprof_aid_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SysprofAid *self = SYSPROF_AID (object);

  switch (prop_id)
    {
    case PROP_DISPLAY_NAME:
      g_value_set_string (value, sysprof_aid_get_display_name (self));
      break;

    case PROP_ICON:
      g_value_set_object (value, sysprof_aid_get_icon (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SysprofAid *self = SYSPROF_AID (object);
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DISPLAY_NAME:
      priv->display_name = g_value_dup_string (value);
      break;

    case PROP_ICON:
      priv->icon = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_class_init (SysprofAidClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_aid_finalize;
  object_class->get_property = sysprof_aid_get_property;
  object_class->set_property = sysprof_aid_set_property;

  properties [PROP_DISPLAY_NAME] =
    g_param_spec_string ("display-name",
                         "Display Name",
                         "Display Name",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ICON] =
    g_param_spec_object ("icon",
                         "Icon",
                         "The icon to display",
                         G_TYPE_ICON,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_aid_init (SysprofAid *self)
{
}

/**
 * sysprof_aid_get_display_name:
 * @self: a #SysprofAid
 *
 * Gets the display name as it should be shown to the user.
 *
 * Returns: a string containing the display name
 *
 * Since: 3.34
 */
const gchar *
sysprof_aid_get_display_name (SysprofAid *self)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_AID (self), NULL);

  return priv->display_name;
}

/**
 * sysprof_aid_get_icon:
 *
 * Gets the icon for the aid.
 *
 * Returns: (transfer none) (nullable): a #GIcon or %NULL
 *
 * Since: 3.34
 */
GIcon *
sysprof_aid_get_icon (SysprofAid *self)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_AID (self), NULL);

  return priv->icon;
}
