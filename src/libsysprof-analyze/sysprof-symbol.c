/*
 * sysprof-symbol.c
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

#include "sysprof-symbol-private.h"

G_DEFINE_FINAL_TYPE (SysprofSymbol, sysprof_symbol, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_NAME,
  PROP_BINARY_NICK,
  PROP_BINARY_PATH,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_symbol_finalize (GObject *object)
{
  SysprofSymbol *self = (SysprofSymbol *)object;

  g_clear_pointer (&self->name, g_ref_string_release);
  g_clear_pointer (&self->binary_path, g_ref_string_release);
  g_clear_pointer (&self->binary_nick, g_ref_string_release);

  G_OBJECT_CLASS (sysprof_symbol_parent_class)->finalize (object);
}

static void
sysprof_symbol_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  SysprofSymbol *self = SYSPROF_SYMBOL (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, sysprof_symbol_get_name (self));
      break;

    case PROP_BINARY_NICK:
      g_value_set_string (value, sysprof_symbol_get_binary_nick (self));
      break;

    case PROP_BINARY_PATH:
      g_value_set_string (value, sysprof_symbol_get_binary_path (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_symbol_class_init (SysprofSymbolClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_symbol_finalize;
  object_class->get_property = sysprof_symbol_get_property;

  properties [PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BINARY_NICK] =
    g_param_spec_string ("binary-nick", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BINARY_PATH] =
    g_param_spec_string ("binary-path", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_symbol_init (SysprofSymbol *self)
{
}

const char *
sysprof_symbol_get_name (SysprofSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (self), NULL);

  return self->name;
}

const char *
sysprof_symbol_get_binary_nick (SysprofSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (self), NULL);

  return self->binary_nick;
}

const char *
sysprof_symbol_get_binary_path (SysprofSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (self), NULL);

  return self->binary_path;
}

SysprofSymbol *
_sysprof_symbol_new (GRefString     *name,
                     GRefString     *binary_path,
                     GRefString     *binary_nick,
                     SysprofAddress  begin_address,
                     SysprofAddress  end_address)
{
  SysprofSymbol *self;

  self = g_object_new (SYSPROF_TYPE_SYMBOL, NULL);
  self->name = name;
  self->binary_path = binary_path;
  self->binary_nick = binary_nick;
  self->begin_address = begin_address;
  self->end_address = end_address;
  self->hash = g_str_hash (name);

  /* If we got a path for the symbol, add that to the hash so that we
   * can be sure that we're working with a symbol in the same file when
   * there are collisions.
   *
   * That way, we have a chance of joining symbols from different runtimes
   * and/or containers, but only if they are reasonably the same ABI.
   */
  if (binary_path != NULL)
    {
      const char *base = strrchr (binary_path, '/');

      if (base != NULL)
        self->hash ^= g_str_hash (base);
    }

  return self;
}

gboolean
sysprof_symbol_equal (const SysprofSymbol *a,
                      const SysprofSymbol *b)
{
  return _sysprof_symbol_equal (a, b);
}

guint
sysprof_symbol_hash (const SysprofSymbol *self)
{
  return self->hash;
}
