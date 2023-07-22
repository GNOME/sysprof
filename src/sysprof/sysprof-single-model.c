/* sysprof-single-model.c
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

#include "sysprof-single-model.h"

struct _SysprofSingleModel
{
  GObject  parent_instance;
  GObject *item;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

static guint
get_n_items (GListModel *model)
{
  return SYSPROF_SINGLE_MODEL (model)->item ? 1 : 0;
}

static gpointer
get_item (GListModel *model,
          guint       position)
{
  if (position == 0 && SYSPROF_SINGLE_MODEL (model)->item)
    return g_object_ref (SYSPROF_SINGLE_MODEL (model)->item);
  return NULL;
}

static GType
get_item_type (GListModel *model)
{
  if (SYSPROF_SINGLE_MODEL (model)->item)
    return G_OBJECT_TYPE (SYSPROF_SINGLE_MODEL (model)->item);
  return G_TYPE_OBJECT;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = get_n_items;
  iface->get_item = get_item;
  iface->get_item_type = get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofSingleModel, sysprof_single_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties[N_PROPS];

static void
sysprof_single_model_finalize (GObject *object)
{
  SysprofSingleModel *self = (SysprofSingleModel *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_single_model_parent_class)->finalize (object);
}

static void
sysprof_single_model_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofSingleModel *self = SYSPROF_SINGLE_MODEL (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, sysprof_single_model_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_single_model_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofSingleModel *self = SYSPROF_SINGLE_MODEL (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      sysprof_single_model_set_item (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_single_model_class_init (SysprofSingleModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_single_model_finalize;
  object_class->get_property = sysprof_single_model_get_property;
  object_class->set_property = sysprof_single_model_set_property;

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_single_model_init (SysprofSingleModel *self)
{
}

SysprofSingleModel *
sysprof_single_model_new (void)
{
  return g_object_new (SYSPROF_TYPE_SINGLE_MODEL, NULL);
}

gpointer
sysprof_single_model_get_item (SysprofSingleModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SINGLE_MODEL (self), NULL);

  return self->item;
}

void
sysprof_single_model_set_item (SysprofSingleModel *self,
                               gpointer            item)
{
  g_return_if_fail (SYSPROF_IS_SINGLE_MODEL (self));
  g_return_if_fail (!item || G_IS_OBJECT (item));

  if (self->item == item)
    return;

  if (item)
    g_object_ref (item);

  if (self->item != NULL)
    {
      g_clear_object (&self->item);
      g_list_model_items_changed (G_LIST_MODEL (self), 0, 1, 0);
    }

  self->item = item;

  if (self->item != NULL)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, 0, 1);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM]);
}
