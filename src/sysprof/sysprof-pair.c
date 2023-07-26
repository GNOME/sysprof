/* sysprof-pair.c
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

#include "sysprof-pair.h"

struct _SysprofPair
{
  GObject  parent_instance;
  GObject *first;
  GObject *second;
};

enum {
  PROP_0,
  PROP_FIRST,
  PROP_SECOND,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofPair, sysprof_pair, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_pair_finalize (GObject *object)
{
  SysprofPair *self = (SysprofPair *)object;

  g_clear_object (&self->first);
  g_clear_object (&self->second);

  G_OBJECT_CLASS (sysprof_pair_parent_class)->finalize (object);
}

static void
sysprof_pair_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  SysprofPair *self = SYSPROF_PAIR (object);

  switch (prop_id)
    {
    case PROP_FIRST:
      g_value_set_object (value, self->first);
      break;

    case PROP_SECOND:
      g_value_set_object (value, self->second);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_pair_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  SysprofPair *self = SYSPROF_PAIR (object);

  switch (prop_id)
    {
    case PROP_FIRST:
      self->first = g_value_dup_object (value);
      break;

    case PROP_SECOND:
      self->second = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_pair_class_init (SysprofPairClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_pair_finalize;
  object_class->get_property = sysprof_pair_get_property;
  object_class->set_property = sysprof_pair_set_property;

  properties [PROP_FIRST] =
    g_param_spec_object ("first", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SECOND] =
    g_param_spec_object ("second", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_pair_init (SysprofPair *self)
{
}
