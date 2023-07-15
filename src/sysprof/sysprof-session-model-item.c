/* sysprof-session-model-item.c
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

#include "sysprof-session-model-item.h"

struct _SysprofSessionModelItem
{
  GObject         parent_instance;
  SysprofSession *session;
  GObject        *item;
};

enum {
  PROP_0,
  PROP_SESSION,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSessionModelItem, sysprof_session_model_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_session_model_item_dispose (GObject *object)
{
  SysprofSessionModelItem *self = (SysprofSessionModelItem *)object;

  g_clear_weak_pointer (&self->session);
  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_session_model_item_parent_class)->dispose (object);
}

static void
sysprof_session_model_item_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofSessionModelItem *self = SYSPROF_SESSION_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_session_model_item_get_session (self));
      break;

    case PROP_ITEM:
      g_value_set_object (value, sysprof_session_model_item_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_model_item_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofSessionModelItem *self = SYSPROF_SESSION_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_set_weak_pointer (&self->session, g_value_get_object (value));
      break;

    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_model_item_class_init (SysprofSessionModelItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_session_model_item_dispose;
  object_class->get_property = sysprof_session_model_item_get_property;
  object_class->set_property = sysprof_session_model_item_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_session_model_item_init (SysprofSessionModelItem *self)
{
}

/**
 * sysprof_session_model_item_get_session:
 * @self: a #SysprofSessionModelItem
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_session_model_item_get_session (SysprofSessionModelItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION_MODEL_ITEM (self), NULL);

  return self->session;
}

/**
 * sysprof_session_model_item_get_item:
 * @self: a #SysprofSessionModelItem
 *
 * Gets the underlying item.
 *
 * Returns: (transfer none) (nullable) (type GObject): a #GObject or %NULL
 */
gpointer
sysprof_session_model_item_get_item (SysprofSessionModelItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION_MODEL_ITEM (self), NULL);

  return self->item;
}
