/* sysprof-environ.c
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

#define G_LOG_DOMAIN "sysprof-environ"

#include "config.h"

#include <string.h>

#include "sysprof-environ.h"
#include "sysprof-environ-variable.h"

struct _SysprofEnviron
{
  GObject    parent_instance;
  GPtrArray *variables;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofEnviron, sysprof_environ, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  CHANGED,
  LAST_SIGNAL
};

static guint signals [LAST_SIGNAL];

static void
sysprof_environ_finalize (GObject *object)
{
  SysprofEnviron *self = (SysprofEnviron *)object;

  g_clear_pointer (&self->variables, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_environ_parent_class)->finalize (object);
}

static void
sysprof_environ_class_init (SysprofEnvironClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_environ_finalize;

  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  g_signal_set_va_marshaller (signals [CHANGED],
                              G_TYPE_FROM_CLASS (klass),
                              g_cclosure_marshal_VOID__VOIDv);
}

static void
sysprof_environ_items_changed (SysprofEnviron *self)
{
  g_assert (SYSPROF_IS_ENVIRON (self));

  g_signal_emit (self, signals [CHANGED], 0);
}

static void
sysprof_environ_init (SysprofEnviron *self)
{
  self->variables = g_ptr_array_new_with_free_func (g_object_unref);

  g_signal_connect (self,
                    "items-changed",
                    G_CALLBACK (sysprof_environ_items_changed),
                    NULL);
}

static GType
sysprof_environ_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_ENVIRON_VARIABLE;
}

static gpointer
sysprof_environ_get_item (GListModel *model,
                          guint       position)
{
  SysprofEnviron *self = (SysprofEnviron *)model;

  g_return_val_if_fail (SYSPROF_IS_ENVIRON (self), NULL);
  g_return_val_if_fail (position < self->variables->len, NULL);

  return g_object_ref (g_ptr_array_index (self->variables, position));
}

static guint
sysprof_environ_get_n_items (GListModel *model)
{
  SysprofEnviron *self = (SysprofEnviron *)model;

  g_return_val_if_fail (SYSPROF_IS_ENVIRON (self), 0);

  return self->variables->len;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = sysprof_environ_get_n_items;
  iface->get_item = sysprof_environ_get_item;
  iface->get_item_type = sysprof_environ_get_item_type;
}

static void
sysprof_environ_variable_notify (SysprofEnviron         *self,
                                 GParamSpec             *pspec,
                                 SysprofEnvironVariable *variable)
{
  g_assert (SYSPROF_IS_ENVIRON (self));

  g_signal_emit (self, signals [CHANGED], 0);
}

void
sysprof_environ_setenv (SysprofEnviron *self,
                        const gchar    *key,
                        const gchar    *value)
{
  guint i;

  g_return_if_fail (SYSPROF_IS_ENVIRON (self));
  g_return_if_fail (key != NULL);

  for (i = 0; i < self->variables->len; i++)
    {
      SysprofEnvironVariable *var = g_ptr_array_index (self->variables, i);
      const gchar *var_key = sysprof_environ_variable_get_key (var);

      if (g_strcmp0 (key, var_key) == 0)
        {
          if (value == NULL)
            {
              g_ptr_array_remove_index (self->variables, i);
              g_list_model_items_changed (G_LIST_MODEL (self), i, 1, 0);
              return;
            }

          sysprof_environ_variable_set_value (var, value);
          return;
        }
    }

  if (value != NULL)
    {
      SysprofEnvironVariable *var;
      guint position = self->variables->len;

      var = g_object_new (SYSPROF_TYPE_ENVIRON_VARIABLE,
                          "key", key,
                          "value", value,
                          NULL);
      g_signal_connect_object (var,
                               "notify",
                               G_CALLBACK (sysprof_environ_variable_notify),
                               self,
                               G_CONNECT_SWAPPED);
      g_ptr_array_add (self->variables, var);
      g_list_model_items_changed (G_LIST_MODEL (self), position, 0, 1);
    }
}

const gchar *
sysprof_environ_getenv (SysprofEnviron *self,
                        const gchar    *key)
{
  guint i;

  g_return_val_if_fail (SYSPROF_IS_ENVIRON (self), NULL);
  g_return_val_if_fail (key != NULL, NULL);

  for (i = 0; i < self->variables->len; i++)
    {
      SysprofEnvironVariable *var = g_ptr_array_index (self->variables, i);
      const gchar *var_key = sysprof_environ_variable_get_key (var);

      if (g_strcmp0 (key, var_key) == 0)
        return sysprof_environ_variable_get_value (var);
    }

  return NULL;
}

/**
 * sysprof_environ_get_environ:
 * @self: An #SysprofEnviron
 *
 * Gets the environment as a set of key=value pairs, suitable for use
 * in various GLib process functions.
 *
 * Returns: (transfer full): A newly allocated string array.
 *
 * Since: 3.32
 */
gchar **
sysprof_environ_get_environ (SysprofEnviron *self)
{
  GPtrArray *ar;
  guint i;

  g_return_val_if_fail (SYSPROF_IS_ENVIRON (self), NULL);

  ar = g_ptr_array_new ();

  for (i = 0; i < self->variables->len; i++)
    {
      SysprofEnvironVariable *var = g_ptr_array_index (self->variables, i);
      const gchar *key = sysprof_environ_variable_get_key (var);
      const gchar *value = sysprof_environ_variable_get_value (var);

      if (value == NULL)
        value = "";

      if (key != NULL)
        g_ptr_array_add (ar, g_strdup_printf ("%s=%s", key, value));
    }

  g_ptr_array_add (ar, NULL);

  return (gchar **)g_ptr_array_free (ar, FALSE);
}

SysprofEnviron *
sysprof_environ_new (void)
{
  return g_object_new (SYSPROF_TYPE_ENVIRON, NULL);
}

void
sysprof_environ_remove (SysprofEnviron         *self,
                        SysprofEnvironVariable *variable)
{
  guint i;

  g_return_if_fail (SYSPROF_IS_ENVIRON (self));
  g_return_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (variable));

  for (i = 0; i < self->variables->len; i++)
    {
      SysprofEnvironVariable *item = g_ptr_array_index (self->variables, i);

      if (item == variable)
        {
          g_ptr_array_remove_index (self->variables, i);
          g_list_model_items_changed (G_LIST_MODEL (self), i, 1, 0);
          break;
        }
    }
}

void
sysprof_environ_append (SysprofEnviron         *self,
                        SysprofEnvironVariable *variable)
{
  guint position;

  g_return_if_fail (SYSPROF_IS_ENVIRON (self));
  g_return_if_fail (SYSPROF_IS_ENVIRON_VARIABLE (variable));

  position = self->variables->len;

  g_signal_connect_object (variable,
                           "notify",
                           G_CALLBACK (sysprof_environ_variable_notify),
                           self,
                           G_CONNECT_SWAPPED);
  g_ptr_array_add (self->variables, g_object_ref (variable));
  g_list_model_items_changed (G_LIST_MODEL (self), position, 0, 1);
}

/**
 * sysprof_environ_copy:
 * @self: An #SysprofEnviron
 *
 * Copies the contents of #SysprofEnviron into a newly allocated #SysprofEnviron.
 *
 * Returns: (transfer full): An #SysprofEnviron.
 *
 * Since: 3.32
 */
SysprofEnviron *
sysprof_environ_copy (SysprofEnviron *self)
{
  g_autoptr(SysprofEnviron) copy = NULL;

  g_return_val_if_fail (SYSPROF_IS_ENVIRON (self), NULL);

  copy = sysprof_environ_new ();
  sysprof_environ_copy_into (self, copy, TRUE);

  return g_steal_pointer (&copy);
}

void
sysprof_environ_copy_into (SysprofEnviron *self,
                           SysprofEnviron *dest,
                           gboolean        replace)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON (self));
  g_return_if_fail (SYSPROF_IS_ENVIRON (dest));

  for (guint i = 0; i < self->variables->len; i++)
    {
      SysprofEnvironVariable *var = g_ptr_array_index (self->variables, i);
      const gchar *key = sysprof_environ_variable_get_key (var);
      const gchar *value = sysprof_environ_variable_get_value (var);

      if (replace || sysprof_environ_getenv (dest, key) == NULL)
        sysprof_environ_setenv (dest, key, value);
    }
}

/**
 * ide_environ_parse:
 * @pair: the KEY=VALUE pair
 * @key: (out) (optional): a location for a @key
 * @value: (out) (optional): a location for a @value
 *
 * Parses a KEY=VALUE style key-pair into @key and @value.
 *
 * Returns: %TRUE if @pair was successfully parsed
 *
 * Since: 3.32
 */
gboolean
ide_environ_parse (const gchar  *pair,
                   gchar       **key,
                   gchar       **value)
{
  const gchar *eq;

  g_return_val_if_fail (pair != NULL, FALSE);

  if (key != NULL)
    *key = NULL;

  if (value != NULL)
    *value = NULL;

  if ((eq = strchr (pair, '=')))
    {
      if (key != NULL)
        *key = g_strndup (pair, eq - pair);

      if (value != NULL)
        *value = g_strdup (eq + 1);

      return TRUE;
    }

  return FALSE;
}
