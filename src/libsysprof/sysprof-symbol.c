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
  PROP_BINARY_NICK,
  PROP_BINARY_PATH,
  PROP_KIND,
  PROP_NAME,
  PROP_TOOLTIP_TEXT,
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

    case PROP_KIND:
      g_value_set_enum (value, sysprof_symbol_get_kind (self));
      break;

    case PROP_TOOLTIP_TEXT:
      g_value_take_string (value, sysprof_symbol_dup_tooltip_text (self));
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

  properties [PROP_TOOLTIP_TEXT] =
    g_param_spec_string ("tooltip-text", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_KIND] =
    g_param_spec_enum ("kind", NULL, NULL,
                       SYSPROF_TYPE_SYMBOL_KIND,
                       SYSPROF_SYMBOL_KIND_USER,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_symbol_init (SysprofSymbol *self)
{
  self->kind = SYSPROF_SYMBOL_KIND_USER;
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

static gboolean
sniff_maybe_kernel_process (const char *str)
{
  if (g_str_has_prefix (str, "kworker/") ||
      g_str_equal (str, "rcu_preempt") ||
      g_str_has_prefix (str, "migration/") ||
      g_str_has_prefix (str, "dmcrypt_write/") ||
      g_str_has_prefix (str, "hwrng") ||
      g_str_has_prefix (str, "irq/") ||
      g_str_has_prefix (str, "ksoftirqd/"))
    return TRUE;

  return FALSE;
}

SysprofSymbol *
_sysprof_symbol_new (GRefString        *name,
                     GRefString        *binary_path,
                     GRefString        *binary_nick,
                     SysprofAddress     begin_address,
                     SysprofAddress     end_address,
                     SysprofSymbolKind  kind)
{
  SysprofSymbol *self;

  if (binary_nick != NULL && binary_nick[0] == 0)
    binary_nick = NULL;

  if (binary_path != NULL && binary_path[0] == 0)
    binary_path = NULL;

  self = g_object_new (SYSPROF_TYPE_SYMBOL, NULL);
  self->name = name;
  self->binary_path = binary_path;
  self->binary_nick = binary_nick;
  self->begin_address = begin_address;
  self->end_address = end_address;
  self->simple_hash = g_str_hash (name);
  self->kind = kind;

  if (self->kind == SYSPROF_SYMBOL_KIND_PROCESS)
    self->is_kernel_process = sniff_maybe_kernel_process (name);

  if (binary_nick != NULL)
    self->simple_hash ^= g_str_hash (binary_nick);

  self->hash = self->simple_hash;

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

SysprofSymbolKind
sysprof_symbol_get_kind (SysprofSymbol *self)
{
  return self ? self->kind : 0;
}

/**
 * sysprof_symbol_dup_tooltip_text:
 * @self: a #SysprofSymbol
 *
 * Returns: (transfer full): the tooltip text
 */
char *
sysprof_symbol_dup_tooltip_text (SysprofSymbol *self)
{
  GString *str;

  g_return_val_if_fail (SYSPROF_IS_SYMBOL (self), NULL);

  str = g_string_new (self->name);

  if (!g_str_has_prefix (str->str, "In File") && self->binary_path)
    g_string_append_printf (str, " [%s+0x%"G_GINT64_MODIFIER"x]", self->binary_path, self->begin_address);

  return g_string_free (str, FALSE);
}

G_DEFINE_ENUM_TYPE (SysprofSymbolKind, sysprof_symbol_kind,
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_ROOT, "root"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_PROCESS, "process"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH, "context-switch"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_USER, "user"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_KERNEL, "kernel"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_SYMBOL_KIND_UNWINDABLE, "unwindable"))
