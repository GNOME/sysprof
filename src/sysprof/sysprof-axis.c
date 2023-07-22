/* sysprof-axis.c
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

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

enum {
  RANGE_CHANGED,
  N_SIGNALS
};

G_DEFINE_ABSTRACT_TYPE (SysprofAxis, sysprof_axis, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static double
sysprof_axis_real_normalize (SysprofAxis  *axis,
                             const GValue *value)
{
  return .0;
}

static void
sysprof_axis_finalize (GObject *object)
{
  SysprofAxis *self = (SysprofAxis *)object;

  g_clear_pointer (&self->title, g_free);

  G_OBJECT_CLASS (sysprof_axis_parent_class)->finalize (object);
}

static void
sysprof_axis_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  SysprofAxis *self = SYSPROF_AXIS (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, sysprof_axis_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_axis_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  SysprofAxis *self = SYSPROF_AXIS (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      sysprof_axis_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_axis_class_init (SysprofAxisClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_axis_finalize;
  object_class->get_property = sysprof_axis_get_property;
  object_class->set_property = sysprof_axis_set_property;

  klass->normalize = sysprof_axis_real_normalize;

  properties [PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * SysprofAxis::range-changed:
   *
   * This signal is emitted when the range of the axis has changed.
   */
  signals[RANGE_CHANGED] =
    g_signal_new ("range-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
sysprof_axis_init (SysprofAxis *self)
{
}

const char *
sysprof_axis_get_title (SysprofAxis *self)
{
  g_return_val_if_fail (SYSPROF_IS_AXIS (self), NULL);

  return self->title;
}

void
sysprof_axis_set_title (SysprofAxis *self,
                        const char  *title)
{
  g_return_if_fail (SYSPROF_IS_AXIS (self));

  if (g_set_str (&self->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

void
_sysprof_axis_emit_range_changed (SysprofAxis *self)
{
  g_return_if_fail (SYSPROF_IS_AXIS (self));

  g_signal_emit (self, signals[RANGE_CHANGED], 0);
}
