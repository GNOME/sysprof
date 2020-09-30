/* sysprof-zoom-manager.c
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

#define G_LOG_DOMAIN "sysprof-zoom-manager"

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <math.h>
#include <sysprof-capture.h>

#include "sysprof-zoom-manager.h"

#define DEFAULT_PIXELS_PER_SEC (20.0)

struct _SysprofZoomManager
{
  GObject parent_instance;

  GtkAdjustment *adjustment;
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
  PROP_ZOOM_LABEL,
  N_PROPS
};

static void action_group_iface_init (GActionGroupInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofZoomManager, sysprof_zoom_manager, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP, action_group_iface_init))

static GParamSpec *properties [N_PROPS];
static gdouble zoom_levels[] = {
  0.3, 0.5, 0.67, 0.80, 0.90,
  1.0, 1.5, 2.0, 2.5, 3.0,
  5.0, 10.0, 20.0, 30.0, 50.0,
};

static void
sysprof_zoom_manager_zoom_in_action (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  SysprofZoomManager *self = user_data;
  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  sysprof_zoom_manager_zoom_in (self);
}

static void
sysprof_zoom_manager_zoom_out_action (GSimpleAction *action,
                                      GVariant      *param,
                                      gpointer       user_data)
{
  SysprofZoomManager *self = user_data;
  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  sysprof_zoom_manager_zoom_out (self);
}

static void
sysprof_zoom_manager_zoom_one_action (GSimpleAction *action,
                                      GVariant      *param,
                                      gpointer       user_data)
{
  SysprofZoomManager *self = user_data;
  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  sysprof_zoom_manager_reset (self);
}

static void
sysprof_zoom_manager_zoom_action (GSimpleAction *action,
                                  GVariant      *param,
                                  gpointer       user_data)
{
  SysprofZoomManager *self = user_data;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_DOUBLE));

  sysprof_zoom_manager_set_zoom (self, g_variant_get_double (param));
}

static void
sysprof_zoom_manager_value_changed_cb (SysprofZoomManager *self,
                                       GtkAdjustment      *adjustment)
{
  gdouble value;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  g_assert (GTK_IS_ADJUSTMENT (adjustment));

  value = gtk_adjustment_get_value (adjustment);

  if (value == 0.0)
    value = 1.0;
  else if (value > 0.0)
    value = (value + 1.0) * (value + 1.0);
  else
    value = 1.0 / ABS (value);

  sysprof_zoom_manager_set_zoom (self, value);
}

static void
sysprof_zoom_manager_finalize (GObject *object)
{
  SysprofZoomManager *self = (SysprofZoomManager *)object;

  g_clear_object (&self->actions);
  g_clear_object (&self->adjustment);

  G_OBJECT_CLASS (sysprof_zoom_manager_parent_class)->finalize (object);
}

static void
sysprof_zoom_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofZoomManager *self = SYSPROF_ZOOM_MANAGER (object);

  switch (prop_id)
    {
    case PROP_MIN_ZOOM:
      g_value_set_double (value, sysprof_zoom_manager_get_min_zoom (self));
      break;

    case PROP_MAX_ZOOM:
      g_value_set_double (value, sysprof_zoom_manager_get_max_zoom (self));
      break;

    case PROP_ZOOM:
      g_value_set_double (value, sysprof_zoom_manager_get_zoom (self));
      break;

    case PROP_ZOOM_LABEL:
      g_value_take_string (value, sysprof_zoom_manager_get_zoom_label (self));
      break;

    case PROP_CAN_ZOOM_IN:
      g_value_set_boolean (value, sysprof_zoom_manager_get_can_zoom_in (self));
      break;

    case PROP_CAN_ZOOM_OUT:
      g_value_set_boolean (value, sysprof_zoom_manager_get_can_zoom_out (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_zoom_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofZoomManager *self = SYSPROF_ZOOM_MANAGER (object);

  switch (prop_id)
    {
    case PROP_MIN_ZOOM:
      sysprof_zoom_manager_set_min_zoom (self, g_value_get_double (value));
      break;

    case PROP_MAX_ZOOM:
      sysprof_zoom_manager_set_max_zoom (self, g_value_get_double (value));
      break;

    case PROP_ZOOM:
      sysprof_zoom_manager_set_zoom (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_zoom_manager_class_init (SysprofZoomManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_zoom_manager_finalize;
  object_class->get_property = sysprof_zoom_manager_get_property;
  object_class->set_property = sysprof_zoom_manager_set_property;

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

  properties [PROP_ZOOM_LABEL] =
    g_param_spec_string ("zoom-label", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_zoom_manager_init (SysprofZoomManager *self)
{
  static const GActionEntry entries[] = {
    { "zoom-in", sysprof_zoom_manager_zoom_in_action },
    { "zoom-out", sysprof_zoom_manager_zoom_out_action },
    { "zoom-one", sysprof_zoom_manager_zoom_one_action },
    { "zoom", NULL, "d", "1.0", sysprof_zoom_manager_zoom_action },
  };
  GAction *action;

  self->min_zoom = 0.00001;
  self->max_zoom = 10000.0;
  self->zoom = 1.0;

  self->adjustment = g_object_ref_sink (gtk_adjustment_new (0, -10, 10, 1, 3, 0));

  g_signal_connect_object (self->adjustment,
                           "value-changed",
                           G_CALLBACK (sysprof_zoom_manager_value_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

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

SysprofZoomManager *
sysprof_zoom_manager_new (void)
{
  return g_object_new (SYSPROF_TYPE_ZOOM_MANAGER, NULL);
}

gboolean
sysprof_zoom_manager_get_can_zoom_in (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), FALSE);

  return self->max_zoom == 0.0 || self->zoom < self->max_zoom;
}

gboolean
sysprof_zoom_manager_get_can_zoom_out (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), FALSE);

  return self->min_zoom == 0.0 || self->zoom > self->min_zoom;
}

gboolean
sysprof_zoom_manager_get_min_zoom (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), FALSE);

  return self->min_zoom;
}

gboolean
sysprof_zoom_manager_get_max_zoom (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), FALSE);

  return self->max_zoom;
}

void
sysprof_zoom_manager_set_min_zoom (SysprofZoomManager *self,
                                   gdouble             min_zoom)
{
  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  if (min_zoom != self->min_zoom)
    {
      self->min_zoom = min_zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MIN_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_OUT]);
    }
}

void
sysprof_zoom_manager_set_max_zoom (SysprofZoomManager *self,
                                   gdouble             max_zoom)
{
  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  if (max_zoom != self->max_zoom)
    {
      self->max_zoom = max_zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MAX_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_IN]);
    }
}

void
sysprof_zoom_manager_zoom_in (SysprofZoomManager *self)
{
  gdouble zoom;

  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  if (!sysprof_zoom_manager_get_can_zoom_in (self))
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

  sysprof_zoom_manager_set_zoom (self, zoom);
}

void
sysprof_zoom_manager_zoom_out (SysprofZoomManager *self)
{
  gdouble zoom;

  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  if (!sysprof_zoom_manager_get_can_zoom_out (self))
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

  sysprof_zoom_manager_set_zoom (self, zoom);
}

void
sysprof_zoom_manager_reset (SysprofZoomManager *self)
{
  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  sysprof_zoom_manager_set_zoom (self, 1.0);
}

gdouble
sysprof_zoom_manager_get_zoom (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), 0.0);

  return self->zoom;
}

void
sysprof_zoom_manager_set_zoom (SysprofZoomManager *self,
                               gdouble             zoom)
{
  gdouble min_zoom;
  gdouble max_zoom;

  g_return_if_fail (SYSPROF_IS_ZOOM_MANAGER (self));

  min_zoom = (self->min_zoom == 0.0) ? -G_MAXDOUBLE : self->min_zoom;
  max_zoom = (self->max_zoom == 0.0) ? G_MAXDOUBLE : self->max_zoom;

  zoom = CLAMP (zoom, min_zoom, max_zoom);

  if (zoom == 0.0)
    zoom = 1.0;

  if (zoom != self->zoom)
    {
      g_autoptr(GVariant) state = NULL;
      gdouble adj;

      self->zoom = zoom;
      state = g_variant_take_ref (g_variant_new_double (zoom));

      g_object_set (g_action_map_lookup_action (G_ACTION_MAP (self->actions), "zoom"),
                    "state", state,
                    NULL);

      /* Convert to form used by adjustment */
      if (zoom == 1.0)
        adj = 0.0;
      else if (zoom > 1.0)
        adj = sqrt (zoom) - 1.0;
      else
        adj = -1 / zoom;

      g_signal_handlers_block_matched (self->adjustment, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                       sysprof_zoom_manager_value_changed_cb, self);
      gtk_adjustment_set_value (self->adjustment, adj);
      g_signal_handlers_unblock_matched (self->adjustment, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                         sysprof_zoom_manager_value_changed_cb, self);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_IN]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_ZOOM_OUT]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_LABEL]);
    }
}

static gchar **
sysprof_zoom_manager_list_actions (GActionGroup *action_group)
{
  SysprofZoomManager *self = (SysprofZoomManager *)action_group;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));

  return g_action_group_list_actions (G_ACTION_GROUP (self->actions));
}

static gboolean
sysprof_zoom_manager_query_action (GActionGroup        *action_group,
                                   const gchar         *action_name,
                                   gboolean            *enabled,
                                   const GVariantType **parameter_type,
                                   const GVariantType **state_type,
                                   GVariant           **state_hint,
                                   GVariant           **state)
{
  SysprofZoomManager *self = (SysprofZoomManager *)action_group;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
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
sysprof_zoom_manager_change_action_state (GActionGroup *action_group,
                                          const gchar  *action_name,
                                          GVariant     *value)
{
  SysprofZoomManager *self = (SysprofZoomManager *)action_group;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  g_assert (action_name != NULL);

  g_action_group_change_action_state (G_ACTION_GROUP (self->actions), action_name, value);
}

static void
sysprof_zoom_manager_activate_action (GActionGroup *action_group,
                                      const gchar  *action_name,
                                      GVariant     *parameter)
{
  SysprofZoomManager *self = (SysprofZoomManager *)action_group;

  g_assert (SYSPROF_IS_ZOOM_MANAGER (self));
  g_assert (action_name != NULL);

  g_action_group_activate_action (G_ACTION_GROUP (self->actions), action_name, parameter);
}

static void
action_group_iface_init (GActionGroupInterface *iface)
{
  iface->list_actions = sysprof_zoom_manager_list_actions;
  iface->query_action = sysprof_zoom_manager_query_action;
  iface->change_action_state = sysprof_zoom_manager_change_action_state;
  iface->activate_action = sysprof_zoom_manager_activate_action;
}

/**
 * sysprof_zoom_manager_get_zoom_level:
 * @self: a #SysprofZoomManager
 *
 * Returns: (transfer full): a string containing the zoom percentage label
 *
 * Since: 3.34
 */
gchar *
sysprof_zoom_manager_get_zoom_label (SysprofZoomManager *self)
{
  gdouble percent;

  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), NULL);

  percent = self->zoom * 100.0;

  if (percent < 1.0)
    return g_strdup_printf ("%0.2lf%%", percent);
  else
    return g_strdup_printf ("%d%%", (gint)percent);
}

gint64
sysprof_zoom_manager_get_duration_for_width (SysprofZoomManager *self,
                                             gint                width)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), 0);

  return SYSPROF_NSEC_PER_SEC * ((gdouble)width / (DEFAULT_PIXELS_PER_SEC * self->zoom));
}

gint
sysprof_zoom_manager_get_width_for_duration (SysprofZoomManager *self,
                                             gint64              duration)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), 0);

  return (gdouble)duration / (gdouble)SYSPROF_NSEC_PER_SEC * DEFAULT_PIXELS_PER_SEC * self->zoom;
}

gdouble
sysprof_zoom_manager_fit_zoom_for_duration (SysprofZoomManager *self,
                                            gint64              duration,
                                            gint                width)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), 1.0);
  g_return_val_if_fail (duration >= 0, 1.0);
  g_return_val_if_fail (width >= 0, 1.0);

  return (width / DEFAULT_PIXELS_PER_SEC) / (duration / (gdouble)SYSPROF_NSEC_PER_SEC);
}

gdouble
sysprof_zoom_manager_get_offset_at_time (SysprofZoomManager *self,
                                         gint64              offset,
                                         gint                width)
{
  gint64 full_duration;
  gdouble ratio;

  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), 0.0);

  full_duration = sysprof_zoom_manager_get_duration_for_width (self, width);
  ratio = offset / (gdouble)full_duration;
  return ratio * width;
}

gchar *
_sysprof_format_duration (gint64 duration)
{
  gboolean negative = duration < 0;

  if (duration == 0)
    return g_strdup ("0");

  duration = ABS (duration);

  if (duration < SYSPROF_NSEC_PER_SEC)
    return g_strdup_printf ("%s%.3lf msec",
                            negative ? "-" : "",
                            (duration / (gdouble)SYSPROF_NSEC_PER_SEC * 1000L));
  else
    return g_strdup_printf ("%s%.4lf seconds",
                            negative ? "-" : "",
                            (duration / (gdouble)SYSPROF_NSEC_PER_SEC));
}

/**
 * sysprof_zoom_manager_get_adjustment:
 *
 * Gets an adjustment for zoom controls.
 *
 * Returns: (transfer none): a #GtkAdjustment
 *
 * Since: 3.34
 */
GtkAdjustment *
sysprof_zoom_manager_get_adjustment (SysprofZoomManager *self)
{
  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (self), NULL);

  return self->adjustment;
}
