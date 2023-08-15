/* sysprof-document-bitset-index.c
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

#include "sysprof-document-bitset-index-private.h"

struct _SysprofDocumentBitsetIndex
{
  GObject parent_instance;
  GListModel *model;
  EggBitset *bitset;
  GType type;
};

static GType
sysprof_document_bitset_index_get_item_type (GListModel *model)
{
  SysprofDocumentBitsetIndex *self = SYSPROF_DOCUMENT_BITSET_INDEX (model);

  if (self->type)
    return self->type;

  if (self->model != NULL)
    return g_list_model_get_item_type (self->model);

  return G_TYPE_OBJECT;
}

static guint
sysprof_document_bitset_index_get_n_items (GListModel *model)
{
  SysprofDocumentBitsetIndex *self = SYSPROF_DOCUMENT_BITSET_INDEX (model);

  if (self->bitset != NULL)
    return egg_bitset_get_size (self->bitset);

  return 0;
}

static gpointer
sysprof_document_bitset_index_get_item (GListModel *model,
                                        guint       position)
{
  SysprofDocumentBitsetIndex *self = SYSPROF_DOCUMENT_BITSET_INDEX (model);

  if (self->model == NULL || self->bitset == NULL)
    return NULL;

  if (position >= egg_bitset_get_size (self->bitset))
    return NULL;

  return g_list_model_get_item (self->model,
                                egg_bitset_get_nth (self->bitset, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item = sysprof_document_bitset_index_get_item;
  iface->get_item_type = sysprof_document_bitset_index_get_item_type;
  iface->get_n_items = sysprof_document_bitset_index_get_n_items;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentBitsetIndex, sysprof_document_bitset_index, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  PROP_0,
  PROP_N_ITEMS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
sysprof_document_bitset_index_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  SysprofDocumentBitsetIndex *self = SYSPROF_DOCUMENT_BITSET_INDEX (object);

  switch (prop_id)
    {
    case PROP_N_ITEMS:
      g_value_set_uint (value, g_list_model_get_n_items (G_LIST_MODEL (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_bitset_index_dispose (GObject *object)
{
  SysprofDocumentBitsetIndex *self = (SysprofDocumentBitsetIndex *)object;

  g_clear_pointer (&self->bitset, egg_bitset_unref);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (sysprof_document_bitset_index_parent_class)->dispose (object);
}

static void
sysprof_document_bitset_index_class_init (SysprofDocumentBitsetIndexClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_document_bitset_index_dispose;
  object_class->get_property = sysprof_document_bitset_index_get_property;

  properties[PROP_N_ITEMS] =
    g_param_spec_uint ("n-items", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_bitset_index_init (SysprofDocumentBitsetIndex *self)
{
}

GListModel *
_sysprof_document_bitset_index_new_full (GListModel *model,
                                         EggBitset  *bitset,
                                         GType       type)
{
  SysprofDocumentBitsetIndex *self;

  g_return_val_if_fail (G_IS_LIST_MODEL (model), NULL);
  g_return_val_if_fail (bitset != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_BITSET_INDEX, NULL);
  self->model = g_object_ref (model);
  self->bitset = egg_bitset_ref (bitset);
  self->type = type;

  return G_LIST_MODEL (self);
}

GListModel *
_sysprof_document_bitset_index_new (GListModel *model,
                                    EggBitset  *bitset)
{
  return _sysprof_document_bitset_index_new_full (model, bitset, G_TYPE_INVALID);
}
