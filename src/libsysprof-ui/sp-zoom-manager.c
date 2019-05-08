/* sp-zoom-manager.c
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

#define G_LOG_DOMAIN "sp-zoom-manager"

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "sp-zoom-manager.h"

struct _SpZoomManager
{
  GObject parent_instance;

  GSimpleActionGroup *actions;

  gdouble min_zoom;
  gdouble max_zoom;
  gdouble zoom;
} __attribute__((aligned(8)));

enum {
  PROP_0,
  PROP_CAN_ZOOM_IN,
  PROP_CAN_ZOOM_OUT,
  PROP_MIN_ZOOM,
  PROP_MAX_ZOOM,
  PROP_ZOOM,
  N_PROPS
};

static void action_group_iface_init (GActionGroupInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpZoomManager, sp_zoom_manager, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP, action_group_iface_init))

static GParamSpec *properties [N_PROPS];
static gdouble zoom_levels[] = {
  0.3,
  0.5,
  0.67,
  0.80,
  0.90,
  1.0,
  1.1,
  1.2,
  1.33,
  1.5,
  1.7,
  2.0,
  2.4,
  3.0,
  5.0,
  10.0,
  20.0,
  30.0,
  50.0,
};

static void
sp_zoom_manager_zoom_in_action (GSimpleAction *action,
                                GVariant *param,
                                gpointer user_data)
{
  SpZoomManager *self = user_data;
  g_assert (SP_IS_ZOOM_MANAGER (self));
  sp_zoom_manager_zoom_in (self);
}

static void
sp_zoom_manager_zoom_out_action (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  SpZoomManager *self = user_data;
  g_assert (SP_IS_ZOOM_MANAGER (self));
  sp_zoom_manager_zoom_out (self);
}

static void
sp_zoom_manager_zoom_one_action (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  SpZoomManager *self = user_data;
  g_assert (SP_IS_ZOOM_MANAGER (self));
  sp_zoom_manager_reset (self);
}

static void
sp_zoom_manager_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SpZoomManager *self = SP_ZOOM_MANAGER (object);

  switch (prop_id)
    {
    case PROP_MIN_ZOOM:
      g_value_set_double (value, sp_zoom_manager_get_min_zoom (self));
      break;

    case PROP_MAX_ZOOM:
      g_value_set_double (value, sp_zoom_manager_get_max_zoom (self));
      break;

    case PROP_ZOOM:
      g_value_set_double (value, sp_zoom_manager_get_zoom (self));
      break;

    case PROP_CAN_ZOOM_IN:
      g_value_set_boolean (value, sp_zoom_manager_get_can_zoom_in (self));
      break;

    case PROP_CAN_ZOOM_OUT:
      g_value_set_boolean (value, sp_zoom_manager_get_can_zoom_out (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_zoom_manager_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SpZoomManager *self = SP_ZOOM_MANAGER (object);

  switch (prop_id)
    {
    case PROP_MIN_ZOOM:
      sp_zoom_manager_set_min_zoom (self, g_value_get_double (value));
      break;

    case PROP_MAX_ZOOM:
      sp_zoom_manager_set_max_zoom (self, g_value_get_double (value));
      break;

    case PROP_ZOOM:
      sp_zoom_manager_set_zoom (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_zoom_manager_class_init (SpZoomManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sp_zoom_manager_get_property;
  object_class->set_property = sp_zoom_manager_set_property;

  properties [PROP_CAN_ZOOM_IN] =
    g_param_spec_boolean ("can-zoom-in",
                          "Can Zoom In",
                          "Can Zoom In",
                          TRUE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_ZOOM_OUT] =
    g_param_spec_boolean ("can-zoom-out",
                          "Can Zoom Out",
                          "Can Zoom Out",
                          TRUE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MIN_ZOOM] =
    g_param_spec_double ("min-zoom",
                         "Min Zoom",
                         "The minimum zoom to apply",
                         -G_MAXDOUBLE,
                         G_MAXDOUBLE,
                         0.0,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MAX_ZOOM] =
    g_param_spec_double ("max-zoom",
                         "Max Zoom",
                         "The maximum zoom to apply",
                         -G_MAXDOUBLE,
                         G_MAXDOUBLE,
                         0.0,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM] =
    g_param_spec_double ("zoom",
                         "Zoom",
                         "The current zoom level",
                         -G_MAXDOUBLE,
                         G_MAXDOUBLE,
                         1.0,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_zoom_manager_init (SpZoomManager *self)
{
  static const GActionEntry entries[] = {
    { "zoom-in", sp_zoom_manager_zoom_in_action },
    { "zoom-out", sp_zoom_manager_zoom_out_action },
    { "zoom-one", sp_zoom_manager_zoom_one_action },
  };
  GAction *action;

  self->min_zoom = 0.0;
  self->max_zoom = 0.0;
  self->zoom = 1.0;

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);

  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "zoom-in");
  g_object_bind_property (self, "can-zoom-in", action, "enabled", G_BINDING_SYNC_CREATE);

  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "zoom-out");
  g_object_bind_property (self, "can-zoom-out", action, "enabled", G_BINDING_SYNC_CREATE);
}

SpZoomManager *
sp_zoom_manager_new (void)
{
  return g_object_new (SP_TYPE_ZOOM_MANAGER, NULL);
}

gboolean
sp_zoom_manager_get_can_zoom_in (SpZoomManager *self)
{
  g_return_val_if_fail (SP_IS_ZOOM_MANAGER (self), FALSE);

  return self->max_zoom == 0.0 || self->zoom < self->max_zoom;
}

gboolean
sp_zoom_manager_get_can_zoom_out (SpZoomManager *self)
{
  g_return_val_if_fail (SP_IS_ZOOM_MANAGER (self), FALSE);

  return self->min_zoom == 0.0 || self->zoom > self->min_zoom;
}

gboolean
sp_zoom_manager_get_min_zoom (SpZoomManager *self)
{
  g_return_val_if_fail (SP_IS_ZOOM_MANAGER (self), FALSE);

  return self->min_zoom;
}

gboolean
sp_zoom_manager_get_max_zoom (SpZoomManager *self)
{
  g_return_val_if_fail (SP_IS_ZOOM_MANAGER (self), FALSE);

  return self->max_zoom;
}

void
sp_zoom_manager_set_min_zoom (SpZoomManager *self,
                              gdouble        min_zoom)
{
  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  if (min_zoom != self->min_zoom)
    {
      self->min_zoom = min_zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MIN_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_OUT]);
    }
}

void
sp_zoom_manager_set_max_zoom (SpZoomManager *self,
                              gdouble        max_zoom)
{
  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  if (max_zoom != self->max_zoom)
    {
      self->max_zoom = max_zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MAX_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_IN]);
    }
}

void
sp_zoom_manager_zoom_in (SpZoomManager *self)
{
  gdouble zoom;

  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  if (!sp_zoom_manager_get_can_zoom_in (self))
    return;

  zoom = self->zoom;

  for (guint i = 0; i < G_N_ELEMENTS (zoom_levels); i++)
    {
      if (zoom_levels[i] > zoom)
        {
          zoom = zoom_levels[i];
          break;
        }
    }

  if (zoom == self->zoom)
    zoom *= 2;

  sp_zoom_manager_set_zoom (self, zoom);
}

void
sp_zoom_manager_zoom_out (SpZoomManager *self)
{
  gdouble zoom;

  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  if (!sp_zoom_manager_get_can_zoom_out (self))
    return;

  zoom = self->zoom;

  for (guint i = G_N_ELEMENTS (zoom_levels); i > 0; i--)
    {
      if (zoom_levels[i-1] < zoom)
        {
          zoom = zoom_levels[i-1];
          break;
        }
    }

  if (zoom == self->zoom)
    zoom /= 2.0;

  sp_zoom_manager_set_zoom (self, zoom);
}

void
sp_zoom_manager_reset (SpZoomManager *self)
{
  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  sp_zoom_manager_set_zoom (self, 1.0);
}

gdouble
sp_zoom_manager_get_zoom (SpZoomManager *self)
{
  g_return_val_if_fail (SP_IS_ZOOM_MANAGER (self), 0.0);

  return self->zoom;
}

void
sp_zoom_manager_set_zoom (SpZoomManager *self,
                          gdouble        zoom)
{
  gdouble min_zoom;
  gdouble max_zoom;

  g_return_if_fail (SP_IS_ZOOM_MANAGER (self));

  min_zoom = (self->min_zoom == 0.0) ? -G_MAXDOUBLE : self->min_zoom;
  max_zoom = (self->max_zoom == 0.0) ? G_MAXDOUBLE : self->max_zoom;

  zoom = CLAMP (zoom, min_zoom, max_zoom);

  if (zoom == 0.0)
    zoom = 1.0;

  if (zoom != self->zoom)
    {
      self->zoom = zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_IN]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_OUT]);
    }
}

static gchar **
sp_zoom_manager_list_actions (GActionGroup *action_group)
{
  SpZoomManager *self = (SpZoomManager *)action_group;

  g_assert (SP_IS_ZOOM_MANAGER (self));

  return g_action_group_list_actions (G_ACTION_GROUP (self->actions));
}

static gboolean
sp_zoom_manager_query_action (GActionGroup        *action_group,
                              const gchar         *action_name,
                              gboolean            *enabled,
                              const GVariantType **parameter_type,
                              const GVariantType **state_type,
                              GVariant           **state_hint,
                              GVariant           **state)
{
  SpZoomManager *self = (SpZoomManager *)action_group;

  g_assert (SP_IS_ZOOM_MANAGER (self));
  g_assert (action_name != NULL);

  return g_action_group_query_action (G_ACTION_GROUP (self->actions),
                                      action_name,
                                      enabled,
                                      parameter_type,
                                      state_type,
                                      state_hint,
                                      state);
}

static void
sp_zoom_manager_change_action_state (GActionGroup *action_group,
                                       const gchar  *action_name,
                                       GVariant     *value)
{
  SpZoomManager *self = (SpZoomManager *)action_group;

  g_assert (SP_IS_ZOOM_MANAGER (self));
  g_assert (action_name != NULL);

  g_action_group_change_action_state (G_ACTION_GROUP (self->actions), action_name, value);
}

static void
sp_zoom_manager_activate_action (GActionGroup *action_group,
                                   const gchar  *action_name,
                                   GVariant     *parameter)
{
  SpZoomManager *self = (SpZoomManager *)action_group;

  g_assert (SP_IS_ZOOM_MANAGER (self));
  g_assert (action_name != NULL);

  g_action_group_activate_action (G_ACTION_GROUP (self->actions), action_name, parameter);
}

static void
action_group_iface_init (GActionGroupInterface *iface)
{
  iface->list_actions = sp_zoom_manager_list_actions;
  iface->query_action = sp_zoom_manager_query_action;
  iface->change_action_state = sp_zoom_manager_change_action_state;
  iface->activate_action = sp_zoom_manager_activate_action;
}
