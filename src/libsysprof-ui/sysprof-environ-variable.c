/* sysprof-environ-variable.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "sysprof-environ-variable"

#include "config.h"

#include "sysprof-environ-variable.h"

struct _SysprofEnvironVariable
{
  GObject  parent_instance;
  gchar   *key;
  gchar   *value;
};

G_DEFINE_TYPE (SysprofEnvironVariable, sysprof_environ_variable, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_KEY,
  PROP_VALUE,
  LAST_PROP
};

static GParamSpec *properties [LAST_PROP];

static void
sysprof_environ_variable_finalize (GObject *object)
{
  SysprofEnvironVariable *self = (SysprofEnvironVariable *)object;

  g_clear_pointer (&self->key, g_free);
  g_clear_pointer (&self->value, g_free);

  G_OBJECT_CLASS (sysprof_environ_variable_parent_class)->finalize (object);
}

static void
sysprof_environ_variable_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofEnvironVariable *self = SYSPROF_ENVIRON_VARIABLE(object);

  switch (prop_id)
    {
    case PROP_KEY:
      g_value_set_string (value, self->key);
      break;

    case PROP_VALUE:
      g_value_set_string (value, self->value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
sysprof_environ_variable_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SysprofEnvironVariable *self = SYSPROF_ENVIRON_VARIABLE(object);

  switch (prop_id)
    {
    case PROP_KEY:
      sysprof_environ_variable_set_key (self, g_value_get_string (value));
      break;

    case PROP_VALUE:
      sysprof_environ_variable_set_value (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
sysprof_environ_variable_class_init (SysprofEnvironVariableClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_environ_variable_finalize;
  object_class->get_property = sysprof_environ_variable_get_property;
  object_class->set_property = sysprof_environ_variable_set_property;

  properties [PROP_KEY] =
    g_param_spec_string ("key",
                         "Key",
                         "The key for the environment variable",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_VALUE] =
    g_param_spec_string ("value",
                         "Value",
                         "The value for the environment variable",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
sysprof_environ_variable_init (SysprofEnvironVariable *self)
{
}

const gchar *
sysprof_environ_variable_get_key (SysprofEnvironVariable *self)
{
  g_return_val_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (self), NULL);

  return self->key;
}

void
sysprof_environ_variable_set_key (SysprofEnvironVariable *self,
                                  const gchar            *key)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (self));

  if (g_strcmp0 (key, self->key) != 0)
    {
      g_free (self->key);
      self->key = g_strdup (key);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_KEY]);
    }
}

const gchar *
sysprof_environ_variable_get_value (SysprofEnvironVariable *self)
{
  g_return_val_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (self), NULL);

  return self->value;
}

void
sysprof_environ_variable_set_value (SysprofEnvironVariable *self,
                                    const gchar            *value)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (self));

  if (g_strcmp0 (value, self->value) != 0)
    {
      g_free (self->value);
      self->value = g_strdup (value);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_VALUE]);
    }
}

SysprofEnvironVariable *
sysprof_environ_variable_new (const gchar *key,
                              const gchar *value)
{
  return g_object_new (SYSPROF_TYPE_ENVIRON_VARIABLE,
                       "key", key,
                       "value", value,
                       NULL);
}
