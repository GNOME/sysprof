/* sysprof-value-axis.c
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

#include <math.h>

#include "sysprof-axis-private.h"
#include "sysprof-value-axis.h"

struct _SysprofValueAxis
{
  SysprofAxis parent_instance;
  double min_value;
  double max_value;
  double distance;
};

struct _SysprofValueAxisClass
{
  SysprofAxisClass parent_class;
};

enum {
  PROP_0,
  PROP_MIN_VALUE,
  PROP_MAX_VALUE,
  N_PROPS
};

G_DEFINE_TYPE (SysprofValueAxis, sysprof_value_axis, SYSPROF_TYPE_AXIS)

static GParamSpec *properties [N_PROPS];

static inline double
get_as_double (const GValue *value)
{
  if (G_VALUE_HOLDS_DOUBLE (value))
    return g_value_get_double (value);

  if (G_VALUE_HOLDS_FLOAT (value))
    return g_value_get_float (value);

  if (G_VALUE_HOLDS_INT64 (value))
    return g_value_get_int64 (value);

  if (G_VALUE_HOLDS_UINT64 (value))
    return g_value_get_uint64 (value);

  if (G_VALUE_HOLDS_INT (value))
    return g_value_get_int (value);

  if (G_VALUE_HOLDS_UINT (value))
    return g_value_get_uint (value);

  if (g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE))
    {
      GValue dst = G_VALUE_INIT;

      g_value_init (&dst, G_TYPE_DOUBLE);
      g_value_transform (value, &dst);
      g_value_unset (&dst);

      return g_value_get_double (&dst);
    }

  return .0;
}

static void
sysprof_value_axis_real_get_min_value (SysprofAxis *axis,
                                       GValue      *value)
{
  SysprofValueAxis *self = SYSPROF_VALUE_AXIS (axis);

  g_value_init (value, G_TYPE_DOUBLE);
  g_value_set_double (value, MIN (self->min_value, self->max_value));
}

static double
sysprof_value_axis_normalize (SysprofAxis  *axis,
                              const GValue *value)
{
  SysprofValueAxis *self = (SysprofValueAxis *)axis;
  double v = get_as_double (value);
  double r = (v - self->min_value) / self->distance;

  return r;
}

static gboolean
sysprof_value_axis_is_pathological (SysprofAxis *axis)
{
  SysprofValueAxis *self = SYSPROF_VALUE_AXIS (axis);

  return self->min_value == self->max_value;
}

static void
sysprof_value_axis_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofValueAxis *self = SYSPROF_VALUE_AXIS (object);

  switch (prop_id)
    {
    case PROP_MIN_VALUE:
      g_value_set_double (value, sysprof_value_axis_get_min_value (self));
      break;

    case PROP_MAX_VALUE:
      g_value_set_double (value, sysprof_value_axis_get_max_value (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_value_axis_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofValueAxis *self = SYSPROF_VALUE_AXIS (object);

  switch (prop_id)
    {
    case PROP_MIN_VALUE:
      sysprof_value_axis_set_min_value (self, g_value_get_double (value));
      break;

    case PROP_MAX_VALUE:
      sysprof_value_axis_set_max_value (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_value_axis_class_init (SysprofValueAxisClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofAxisClass *axis_class = SYSPROF_AXIS_CLASS (klass);

  object_class->get_property = sysprof_value_axis_get_property;
  object_class->set_property = sysprof_value_axis_set_property;

  axis_class->get_min_value = sysprof_value_axis_real_get_min_value;
  axis_class->normalize = sysprof_value_axis_normalize;
  axis_class->is_pathological = sysprof_value_axis_is_pathological;

  properties[PROP_MIN_VALUE] =
    g_param_spec_double ("min-value", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MAX_VALUE] =
    g_param_spec_double ("max-value", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_value_axis_init (SysprofValueAxis *self)
{
}

double
sysprof_value_axis_get_min_value (SysprofValueAxis *self)
{
  g_return_val_if_fail (SYSPROF_IS_VALUE_AXIS (self), .0);

  return self->min_value;
}

void
sysprof_value_axis_set_min_value (SysprofValueAxis *self,
                                  double            min_value)
{
  g_return_if_fail (SYSPROF_IS_VALUE_AXIS (self));

  if (min_value != self->min_value)
    {
      self->min_value = min_value;
      self->distance = self->max_value - self->min_value;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_VALUE]);
      _sysprof_axis_emit_range_changed (SYSPROF_AXIS (self));
    }
}

double
sysprof_value_axis_get_max_value (SysprofValueAxis *self)
{
  g_return_val_if_fail (SYSPROF_IS_VALUE_AXIS (self), .0);

  return self->max_value;
}

void
sysprof_value_axis_set_max_value (SysprofValueAxis *self,
                                  double            max_value)
{
  g_return_if_fail (SYSPROF_IS_VALUE_AXIS (self));

  if (max_value != self->max_value)
    {
      self->max_value = max_value;
      self->distance = self->max_value - self->min_value;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
      _sysprof_axis_emit_range_changed (SYSPROF_AXIS (self));
    }
}

SysprofAxis *
sysprof_value_axis_new (double min_value,
                        double max_value)
{
  return g_object_new (SYSPROF_TYPE_VALUE_AXIS,
                       "min-value", min_value,
                       "max-value", max_value,
                       NULL);
}
