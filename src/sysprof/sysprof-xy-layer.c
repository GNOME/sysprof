/* sysprof-xy-layer.c
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

#include "sysprof-axis-private.h"
#include "sysprof-xy-layer-private.h"

enum {
  PROP_0,
  PROP_FLIP_Y,
  PROP_NORMALIZED_X,
  PROP_NORMALIZED_Y,
  PROP_SERIES,
  PROP_X_AXIS,
  PROP_Y_AXIS,
  N_PROPS
};

G_DEFINE_TYPE (SysprofXYLayer, sysprof_xy_layer, SYSPROF_TYPE_CHART_LAYER)

static GParamSpec *properties [N_PROPS];

static void
sysprof_xy_layer_dispose (GObject *object)
{
  SysprofXYLayer *self = (SysprofXYLayer *)object;

  g_clear_object (&self->series_bindings);
  g_clear_object (&self->x_axis);
  g_clear_object (&self->y_axis);
  g_clear_object (&self->normal_x);
  g_clear_object (&self->normal_y);
  g_clear_object (&self->series);

  G_OBJECT_CLASS (sysprof_xy_layer_parent_class)->dispose (object);
}

static void
sysprof_xy_layer_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofXYLayer *self = SYSPROF_XY_LAYER (object);

  switch (prop_id)
    {
    case PROP_SERIES:
      g_value_set_object (value, sysprof_xy_layer_get_series (self));
      break;

    case PROP_X_AXIS:
      g_value_set_object (value, sysprof_xy_layer_get_x_axis (self));
      break;

    case PROP_Y_AXIS:
      g_value_set_object (value, sysprof_xy_layer_get_y_axis (self));
      break;

    case PROP_NORMALIZED_X:
      /* For inspector debugging */
      g_value_set_object (value, self->normal_x);
      break;

    case PROP_NORMALIZED_Y:
      /* For inspector debugging */
      g_value_set_object (value, self->normal_y);
      break;

    case PROP_FLIP_Y:
      g_value_set_boolean (value, sysprof_xy_layer_get_flip_y (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_xy_layer_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SysprofXYLayer *self = SYSPROF_XY_LAYER (object);

  switch (prop_id)
    {
    case PROP_SERIES:
      sysprof_xy_layer_set_series (self, g_value_get_object (value));
      break;

    case PROP_X_AXIS:
      sysprof_xy_layer_set_x_axis (self, g_value_get_object (value));
      break;

    case PROP_Y_AXIS:
      sysprof_xy_layer_set_y_axis (self, g_value_get_object (value));
      break;

    case PROP_FLIP_Y:
      sysprof_xy_layer_set_flip_y (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_xy_layer_class_init (SysprofXYLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_xy_layer_dispose;
  object_class->get_property = sysprof_xy_layer_get_property;
  object_class->set_property = sysprof_xy_layer_set_property;

  properties[PROP_SERIES] =
    g_param_spec_object ("series", NULL, NULL,
                         SYSPROF_TYPE_XY_SERIES,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_X_AXIS] =
    g_param_spec_object ("x-axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_Y_AXIS] =
    g_param_spec_object ("y-axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_NORMALIZED_X] =
    g_param_spec_object ("normalized-x", NULL, NULL,
                         SYSPROF_TYPE_NORMALIZED_SERIES,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_NORMALIZED_Y] =
    g_param_spec_object ("normalized-y", NULL, NULL,
                         SYSPROF_TYPE_NORMALIZED_SERIES,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FLIP_Y] =
    g_param_spec_boolean ("flip-y", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_xy_layer_init (SysprofXYLayer *self)
{
  self->normal_x = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES, NULL);
  g_signal_connect_object (self->normal_x,
                           "items-changed",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);

  self->normal_y = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES, NULL);
  g_signal_connect_object (self->normal_y,
                           "items-changed",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);

  self->series_bindings = g_binding_group_new ();
  g_binding_group_bind (self->series_bindings, "x-expression",
                        self->normal_x, "expression",
                        G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->series_bindings, "y-expression",
                        self->normal_y, "expression",
                        G_BINDING_SYNC_CREATE);
}

void
_sysprof_xy_layer_get_xy (SysprofXYLayer  *self,
                          const double   **x_values,
                          const double   **y_values,
                          guint           *n_values)
{
  guint n_x_values = 0;
  guint n_y_values = 0;

  g_return_if_fail (SYSPROF_IS_XY_LAYER (self));
  g_return_if_fail (x_values != NULL);
  g_return_if_fail (y_values != NULL);
  g_return_if_fail (n_values != NULL);

  if (self->x_axis == NULL || _sysprof_axis_is_pathological (self->x_axis) ||
      self->y_axis == NULL || _sysprof_axis_is_pathological (self->y_axis))
    {
      *n_values = 0;
      *x_values = NULL;
      *y_values = NULL;

      return;
    }

  *x_values = sysprof_normalized_series_get_values (self->normal_x, &n_x_values);
  *y_values = sysprof_normalized_series_get_values (self->normal_y, &n_y_values);
  *n_values = MIN (n_x_values, n_y_values);
}

/**
 * sysprof_xy_layer_get_series:
 * @self: a #SysprofXYLayer
 *
 * Returns: (transfer none) (nullable): a #SysprofXYSeries or %NULL
 */
SysprofXYSeries *
sysprof_xy_layer_get_series (SysprofXYLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_LAYER (self), NULL);

  return self->series;
}

void
sysprof_xy_layer_set_series (SysprofXYLayer  *self,
                             SysprofXYSeries *series)
{
  g_return_if_fail (SYSPROF_IS_XY_LAYER (self));
  g_return_if_fail (!series || SYSPROF_IS_XY_SERIES (series));

  if (!g_set_object (&self->series, series))
    return;

  g_binding_group_set_source (self->series_bindings, series);

  sysprof_normalized_series_set_series (self->normal_x, SYSPROF_SERIES (series));
  sysprof_normalized_series_set_series (self->normal_y, SYSPROF_SERIES (series));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);
}

/**
 * sysprof_xy_layer_get_x_axis:
 * @self: a #SysprofXYLayer
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis or %NULL
 */
SysprofAxis *
sysprof_xy_layer_get_x_axis (SysprofXYLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_LAYER (self), NULL);

  return self->x_axis;
}

void
sysprof_xy_layer_set_x_axis (SysprofXYLayer *self,
                             SysprofAxis    *x_axis)
{
  g_return_if_fail (SYSPROF_IS_XY_LAYER (self));
  g_return_if_fail (!x_axis || SYSPROF_IS_AXIS (x_axis));

  if (!g_set_object (&self->x_axis, x_axis))
    return;

  sysprof_normalized_series_set_axis (self->normal_x, x_axis);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X_AXIS]);
}

/**
 * sysprof_xy_layer_get_y_axis:
 * @self: a #SysprofXYLayer
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis or %NULL
 */
SysprofAxis *
sysprof_xy_layer_get_y_axis (SysprofXYLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_LAYER (self), NULL);

  return self->y_axis;
}

void
sysprof_xy_layer_set_y_axis (SysprofXYLayer *self,
                             SysprofAxis    *y_axis)
{
  g_return_if_fail (SYSPROF_IS_XY_LAYER (self));
  g_return_if_fail (!y_axis || SYSPROF_IS_AXIS (y_axis));

  if (!g_set_object (&self->y_axis, y_axis))
    return;

  sysprof_normalized_series_set_axis (self->normal_y, y_axis);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y_AXIS]);
}

gboolean
sysprof_xy_layer_get_flip_y (SysprofXYLayer *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_LAYER (self), FALSE);

  return self->flip_y;
}

void
sysprof_xy_layer_set_flip_y (SysprofXYLayer *self,
                             gboolean        flip_y)
{
  g_return_if_fail (SYSPROF_IS_XY_LAYER (self));

  flip_y = !!flip_y;

  if (flip_y != self->flip_y)
    {
      self->flip_y = flip_y;
      sysprof_normalized_series_set_inverted (self->normal_y, flip_y);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_Y]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
