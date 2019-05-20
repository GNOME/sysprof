/* sysprof-proxy-aid.c
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

#define G_LOG_DOMAIN "sysprof-proxy-aid"

#include "sysprof-proxy-aid.h"

typedef struct
{
  GBusType bus_type;
  gchar *bus_name;
  gchar *object_path;
} SysprofProxyAidPrivate;

enum {
  PROP_0,
  PROP_BUS_TYPE,
  PROP_BUS_NAME,
  PROP_OBJECT_PATH,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofProxyAid, sysprof_proxy_aid, SYSPROF_TYPE_AID)

static GParamSpec *properties [N_PROPS];

static void
sysprof_proxy_aid_prepare (SysprofAid      *aid,
                           SysprofProfiler *profiler)
{
  SysprofProxyAid *self = (SysprofProxyAid *)aid;
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_PROXY_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_proxy_source_new (priv->bus_type, priv->bus_name, priv->object_path);
  sysprof_profiler_add_source (profiler, source);
}

static void
sysprof_proxy_aid_finalize (GObject *object)
{
  SysprofProxyAid *self = (SysprofProxyAid *)object;
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  g_clear_pointer (&priv->bus_name, g_free);
  g_clear_pointer (&priv->object_path, g_free);

  G_OBJECT_CLASS (sysprof_proxy_aid_parent_class)->finalize (object);
}

static void
sysprof_proxy_aid_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SysprofProxyAid *self = SYSPROF_PROXY_AID (object);
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_BUS_NAME:
      g_value_set_string (value, priv->bus_name);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;

    case PROP_BUS_TYPE:
      g_value_set_enum (value, priv->bus_type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_proxy_aid_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SysprofProxyAid *self = SYSPROF_PROXY_AID (object);

  switch (prop_id)
    {
    case PROP_BUS_NAME:
      sysprof_proxy_aid_set_bus_name (self, g_value_get_string (value));
      break;

    case PROP_BUS_TYPE:
      sysprof_proxy_aid_set_bus_type (self, g_value_get_enum (value));
      break;

    case PROP_OBJECT_PATH:
      sysprof_proxy_aid_set_object_path (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_proxy_aid_class_init (SysprofProxyAidClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  object_class->finalize = sysprof_proxy_aid_finalize;
  object_class->get_property = sysprof_proxy_aid_get_property;
  object_class->set_property = sysprof_proxy_aid_set_property;

  aid_class->prepare = sysprof_proxy_aid_prepare;

  properties [PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUS_NAME] =
    g_param_spec_string ("bus-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUS_TYPE] =
    g_param_spec_enum ("bus-type", NULL, NULL,
                       G_TYPE_BUS_TYPE,
                       G_BUS_TYPE_SESSION,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_proxy_aid_init (SysprofProxyAid *self)
{
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  priv->bus_type = G_BUS_TYPE_SESSION;
  priv->object_path = g_strdup ("/org/gnome/Sysprof3/Profiler");
}

void
sysprof_proxy_aid_set_bus_type (SysprofProxyAid *self,
                                GBusType         bus_type)
{
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PROXY_AID (self));
  g_return_if_fail (bus_type == G_BUS_TYPE_SESSION || bus_type == G_BUS_TYPE_SYSTEM);

  priv->bus_type = bus_type;
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUS_TYPE]);
}

void
sysprof_proxy_aid_set_bus_name (SysprofProxyAid *self,
                                const gchar     *bus_name)
{
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PROXY_AID (self));

  if (g_strcmp0 (bus_name, priv->bus_name) != 0)
    {
      g_free (priv->bus_name);
      priv->bus_name = g_strdup (bus_name);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUS_NAME]);
    }
}

void
sysprof_proxy_aid_set_object_path (SysprofProxyAid *self,
                                   const gchar     *object_path)
{
  SysprofProxyAidPrivate *priv = sysprof_proxy_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PROXY_AID (self));

  if (g_strcmp0 (object_path, priv->object_path) != 0)
    {
      g_free (priv->object_path);
      priv->object_path = g_strdup (object_path);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_OBJECT_PATH]);
    }
}
