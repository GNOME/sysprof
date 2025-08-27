/*
 * sysprof-marks-section-model-item.c
 *
 * Copyright 2025 Georges Basile Stavracas Neto <feaneron@igalia.com>
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

#include "sysprof-marks-section-model-item.h"

#include <gio/gio.h>

struct _SysprofMarksSectionModelItem
{
  GObject parent_instance;

  GObject  *item;
  gboolean  visible;
};

static GType
sysprof_marks_section_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static gpointer
sysprof_marks_section_model_get_item (GListModel *model,
                                      guint       position)
{
  SysprofMarksSectionModelItem *self = (SysprofMarksSectionModelItem *) model;

  if (self->item == NULL)
    return NULL;

  if (G_IS_LIST_MODEL (self->item))
    return g_list_model_get_item (G_LIST_MODEL (self->item), position);
  else
    return g_object_ref (self->item);
}

static guint
sysprof_marks_section_model_get_n_items (GListModel *model)
{
  SysprofMarksSectionModelItem *self = (SysprofMarksSectionModelItem *) model;

  if (self->item == NULL)
    return 0;

  if (G_IS_LIST_MODEL (self->item))
    return g_list_model_get_n_items (G_LIST_MODEL (self->item));
  else
    return 1;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_marks_section_model_get_item_type;
  iface->get_n_items = sysprof_marks_section_model_get_n_items;
  iface->get_item = sysprof_marks_section_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMarksSectionModelItem, sysprof_marks_section_model_item, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  PROP_0,
  PROP_ITEM,
  PROP_VISIBLE,
  N_PROPS,
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_marks_section_model_item_dispose (GObject *object)
{
  SysprofMarksSectionModelItem *self = (SysprofMarksSectionModelItem *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_marks_section_model_item_parent_class)->dispose (object);
}

static void
sysprof_marks_section_model_item_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  SysprofMarksSectionModelItem *self = SYSPROF_MARKS_SECTION_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, sysprof_marks_section_model_item_get_item (self));
      break;

    case PROP_VISIBLE:
      g_value_set_boolean (value, sysprof_marks_section_model_item_get_visible (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_item_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  SysprofMarksSectionModelItem *self = SYSPROF_MARKS_SECTION_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      if (G_IS_LIST_MODEL (self->item))
        g_signal_connect_object (self->item, "items-changed", G_CALLBACK (g_list_model_items_changed), self, G_CONNECT_SWAPPED);
      break;

    case PROP_VISIBLE:
      sysprof_marks_section_model_item_set_visible (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_item_class_init (SysprofMarksSectionModelItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_marks_section_model_item_dispose;
  object_class->get_property = sysprof_marks_section_model_item_get_property;
  object_class->set_property = sysprof_marks_section_model_item_set_property;

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_VISIBLE] =
    g_param_spec_boolean ("visible", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_marks_section_model_item_init (SysprofMarksSectionModelItem *self)
{
  self->visible = TRUE;
}

SysprofMarksSectionModelItem *
sysprof_marks_section_model_item_new (gpointer item)
{
  return g_object_new (SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM,
                       "item", item,
                       NULL);
}

gpointer
sysprof_marks_section_model_item_get_item (SysprofMarksSectionModelItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL_ITEM (self), NULL);

  return self->item;
}

gboolean
sysprof_marks_section_model_item_get_visible (SysprofMarksSectionModelItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL_ITEM (self), FALSE);

  return self->visible;
}

void
sysprof_marks_section_model_item_set_visible (SysprofMarksSectionModelItem *self,
                                              gboolean                      visible)
{
  g_return_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL_ITEM (self));

  visible = !!visible;

  if (visible != self->visible)
    {
      self->visible = visible;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}
