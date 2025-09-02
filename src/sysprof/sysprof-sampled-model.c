/* sysprof-sampled-model.c
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

#include "sysprof-sampled-model.h"

struct _SysprofSampledModel
{
  GObject parent_instance;
  GListModel *model;
  guint max_items;
  guint real_n_items;
};

enum {
  PROP_0,
  PROP_MODEL,
  PROP_MAX_ITEMS,
  N_PROPS
};

static GType
sysprof_sampled_model_get_item_type (GListModel *model)
{
  SysprofSampledModel *self = (SysprofSampledModel *)model;

  if (self->model != NULL)
    return g_list_model_get_item_type (self->model);

  return G_TYPE_OBJECT;
}

static guint
sysprof_sampled_model_get_n_items (GListModel *model)
{
  SysprofSampledModel *self = (SysprofSampledModel *)model;

  if (self->model != NULL)
    return MIN (self->max_items, self->real_n_items);

  return 0;
}

static gpointer
sysprof_sampled_model_get_item (GListModel *model,
                                guint       position)
{
  SysprofSampledModel *self = (SysprofSampledModel *)model;
  guint new_position;

  if (self->model == NULL)
    return NULL;

  if (self->real_n_items <= self->max_items)
    return g_list_model_get_item (self->model, position);

  new_position = position / (double)g_list_model_get_n_items (G_LIST_MODEL (self)) * self->real_n_items;

  return g_list_model_get_item (self->model, new_position);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_sampled_model_get_item_type;
  iface->get_n_items = sysprof_sampled_model_get_n_items;
  iface->get_item = sysprof_sampled_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofSampledModel, sysprof_sampled_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_sampled_model_items_changed (SysprofSampledModel *self,
                                     guint                position,
                                     guint                removed,
                                     guint                added,
                                     GListModel          *model)
{
  guint old_n_items;
  guint new_n_items;

  old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));
  self->real_n_items = g_list_model_get_n_items (model);
  new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);
}

static void
sysprof_sampled_model_dispose (GObject *object)
{
  SysprofSampledModel *self = (SysprofSampledModel *)object;

  sysprof_sampled_model_set_model (self, NULL);

  G_OBJECT_CLASS (sysprof_sampled_model_parent_class)->dispose (object);
}

static void
sysprof_sampled_model_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofSampledModel *self = SYSPROF_SAMPLED_MODEL (object);

  switch (prop_id)
    {
    case PROP_MAX_ITEMS:
      g_value_set_uint (value, sysprof_sampled_model_get_max_items (self));
      break;

    case PROP_MODEL:
      g_value_set_object (value, sysprof_sampled_model_get_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_sampled_model_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofSampledModel *self = SYSPROF_SAMPLED_MODEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      sysprof_sampled_model_set_model (self, g_value_get_object (value));
      break;

    case PROP_MAX_ITEMS:
      sysprof_sampled_model_set_max_items (self, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_sampled_model_class_init (SysprofSampledModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_sampled_model_dispose;
  object_class->get_property = sysprof_sampled_model_get_property;
  object_class->set_property = sysprof_sampled_model_set_property;

  properties[PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MAX_ITEMS] =
    g_param_spec_uint ("max-items", NULL, NULL,
                       1, G_MAXUINT, G_MAXUINT-1,
                       (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_sampled_model_init (SysprofSampledModel *self)
{
  self->max_items = G_MAXUINT-1;
}

SysprofSampledModel *
sysprof_sampled_model_new (GListModel *model,
                           guint       max_items)
{
  SysprofSampledModel *self;

  g_return_val_if_fail (!model || G_IS_LIST_MODEL (model), NULL);

  self = g_object_new (SYSPROF_TYPE_SAMPLED_MODEL,
                       "model", model,
                       "max-items", max_items,
                       NULL);

  if (model)
    g_object_unref (model);

  return self;
}

guint
sysprof_sampled_model_get_max_items (SysprofSampledModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SAMPLED_MODEL (self), 0);

  return self->max_items;
}

void
sysprof_sampled_model_set_max_items (SysprofSampledModel *self,
                                     guint                max_items)
{
  g_return_if_fail (SYSPROF_IS_SAMPLED_MODEL (self));

  if (max_items == 0)
    max_items = G_MAXUINT-1;

  if (self->max_items != max_items)
    {
      guint old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));
      guint new_n_items;

      self->max_items = max_items;
      new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

      if (new_n_items != old_n_items)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ITEMS]);
    }
}

GListModel *
sysprof_sampled_model_get_model (SysprofSampledModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SAMPLED_MODEL (self), NULL);

  return self->model;
}

void
sysprof_sampled_model_set_model (SysprofSampledModel *self,
                                 GListModel          *model)
{
  guint old_n_items;
  guint new_n_items;

  g_return_if_fail (SYSPROF_IS_SAMPLED_MODEL (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  if (self->model)
    {
      self->real_n_items = 0;
      g_signal_handlers_disconnect_by_func (self->model,
                                            G_CALLBACK (sysprof_sampled_model_items_changed),
                                            self);
    }

  g_set_object (&self->model, model);

  if (self->model)
    {
      self->real_n_items = g_list_model_get_n_items (self->model);
      g_signal_connect_object (self->model,
                               "items-changed",
                               G_CALLBACK (sysprof_sampled_model_items_changed),
                               self,
                               G_CONNECT_SWAPPED);
    }

  new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}
