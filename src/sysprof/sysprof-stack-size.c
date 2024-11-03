/*
 * sysprof-stack-size.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-stack-size.h"

struct _SysprofStackSize
{
  GObject parent_instance;
  char *label;
  guint size;
};

enum {
  PROP_0,
  PROP_SIZE,
  PROP_LABEL,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofStackSize, sysprof_stack_size, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
sysprof_stack_size_finalize (GObject *object)
{
  SysprofStackSize *self = (SysprofStackSize *)object;

  g_clear_pointer (&self->label, g_free);

  G_OBJECT_CLASS (sysprof_stack_size_parent_class)->finalize (object);
}

static void
sysprof_stack_size_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofStackSize *self = SYSPROF_STACK_SIZE (object);

  switch (prop_id)
    {
    case PROP_SIZE:
      g_value_set_uint (value, self->size);
      break;

    case PROP_LABEL:
      g_value_set_string (value, self->label);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_stack_size_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofStackSize *self = SYSPROF_STACK_SIZE (object);

  switch (prop_id)
    {
    case PROP_SIZE:
      self->size = g_value_get_uint (value);
      break;

    case PROP_LABEL:
      g_set_str (&self->label, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_stack_size_class_init (SysprofStackSizeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_stack_size_finalize;
  object_class->get_property = sysprof_stack_size_get_property;
  object_class->set_property = sysprof_stack_size_set_property;

  properties[PROP_SIZE] =
    g_param_spec_uint ("size", NULL, NULL,
                       0, (4096*32), 0,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_LABEL] =
    g_param_spec_string ("label", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_stack_size_init (SysprofStackSize *self)
{
}

guint
sysprof_stack_size_get_size (SysprofStackSize *self)
{
  g_return_val_if_fail (SYSPROF_IS_STACK_SIZE (self), 0);

  return self->size;
}

const char *
sysprof_stack_size_get_label (SysprofStackSize *self)
{
  g_return_val_if_fail (SYSPROF_IS_STACK_SIZE (self), NULL);

  return self->label;
}
