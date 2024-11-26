/* sysprof-series.c
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

#include "sysprof-series-private.h"

enum {
  PROP_0,
  PROP_MODEL,
  PROP_TITLE,
  N_PROPS
};

static GType
sysprof_series_get_item_type (GListModel *model)
{
  return SYSPROF_SERIES_GET_CLASS (model)->series_item_type;
}

static guint
sysprof_series_get_n_items (GListModel *model)
{
  SysprofSeries *self = SYSPROF_SERIES (model);

  if (self->model != NULL)
    return g_list_model_get_n_items (self->model);

  return 0;
}

static gpointer
sysprof_series_get_item (GListModel *model,
                         guint       position)
{
  SysprofSeries *self = SYSPROF_SERIES (model);
  gpointer item;

  if (self->model == NULL)
    return NULL;

  item = g_list_model_get_item (self->model, position);

  if (item == NULL)
    return NULL;

  if (SYSPROF_SERIES_GET_CLASS (self)->get_series_item == NULL)
    return item;

  return SYSPROF_SERIES_GET_CLASS (self)->get_series_item (self, position, item);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_series_get_item_type;
  iface->get_n_items = sysprof_series_get_n_items;
  iface->get_item = sysprof_series_get_item;
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (SysprofSeries, sysprof_series, G_TYPE_OBJECT,
                                  G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_series_items_changed_cb (SysprofSeries *self,
                                 guint          position,
                                 guint          removed,
                                 guint          added,
                                 GListModel    *model)
{
  g_assert (SYSPROF_IS_SERIES (self));
  g_assert (G_IS_LIST_MODEL (model));

  SYSPROF_SERIES_GET_CLASS (self)->items_changed (self, model, position, removed, added);

  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
}

static void
sysprof_series_real_items_changed (SysprofSeries *series,
                                   GListModel    *model,
                                   guint          position,
                                   guint          removed,
                                   guint          added)
{
}

static void
sysprof_series_dispose (GObject *object)
{
  SysprofSeries *self = (SysprofSeries *)object;

  sysprof_series_set_model (self, NULL);

  g_clear_pointer (&self->title, g_free);

  G_OBJECT_CLASS (sysprof_series_parent_class)->dispose (object);
}

static void
sysprof_series_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  SysprofSeries *self = SYSPROF_SERIES (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, sysprof_series_get_model (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_series_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_series_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  SysprofSeries *self = SYSPROF_SERIES (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      sysprof_series_set_model (self, g_value_get_object (value));
      break;

    case PROP_TITLE:
      sysprof_series_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_series_class_init (SysprofSeriesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_series_dispose;
  object_class->get_property = sysprof_series_get_property;
  object_class->set_property = sysprof_series_set_property;

  klass->series_item_type = G_TYPE_OBJECT;
  klass->items_changed = sysprof_series_real_items_changed;

  properties[PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_series_init (SysprofSeries *self)
{
}

const char *
sysprof_series_get_title (SysprofSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_SERIES (self), NULL);

  return self->title;
}

void
sysprof_series_set_title (SysprofSeries *self,
                          const char    *title)
{
  g_return_if_fail (SYSPROF_IS_SERIES (self));

  if (g_set_str (&self->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

/**
 * sysprof_series_get_model:
 * @self: a #SysprofSeries
 *
 * Returns: (transfer none) (nullable): a #GListModel
 */
GListModel *
sysprof_series_get_model (SysprofSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_SERIES (self), NULL);

  return self->model;
}

void
sysprof_series_set_model (SysprofSeries *self,
                          GListModel    *model)
{
  g_return_if_fail (SYSPROF_IS_SERIES (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (model == self->model)
    return;

  if (model != NULL)
    g_object_ref (model);

  if (self->model != NULL)
    {
      guint old_len = g_list_model_get_n_items (self->model);

      g_clear_signal_handler (&self->items_changed_handler, self->model);

      if (old_len > 0)
        {
          SYSPROF_SERIES_GET_CLASS (self)->items_changed (self, self->model, 0, old_len, 0);
          g_list_model_items_changed (G_LIST_MODEL (self), 0, old_len, 0);
        }

      g_clear_object (&self->model);
    }

  self->model = model;

  if (model != NULL)
    {
      guint new_len = g_list_model_get_n_items (model);

      self->items_changed_handler =
        g_signal_connect_object (model,
                                 "items-changed",
                                 G_CALLBACK (sysprof_series_items_changed_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

      if (new_len > 0)
        {
          SYSPROF_SERIES_GET_CLASS (self)->items_changed (self, self->model, 0, 0, new_len);
          g_list_model_items_changed (G_LIST_MODEL (self), 0, 0, new_len);
        }
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}
