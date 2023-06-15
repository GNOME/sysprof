/* sysprof-mark-catalog.c
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

#include "sysprof-mark-catalog-private.h"

struct _SysprofMarkCatalog
{
  GObject parent_instance;
  GListModel *items;
  char *name;
  SysprofMarkCatalogKind kind : 1;
} SysprofMarkCatalogPrivate;

enum {
  PROP_0,
  PROP_NAME,
  PROP_KIND,
  N_PROPS
};

static GType
sysprof_mark_catalog_get_item_type (GListModel *model)
{
  return g_list_model_get_item_type (SYSPROF_MARK_CATALOG (model)->items);
}

static guint
sysprof_mark_catalog_get_n_items (GListModel *model)
{
  return g_list_model_get_n_items (SYSPROF_MARK_CATALOG (model)->items);
}

static gpointer
sysprof_mark_catalog_get_item (GListModel *model,
                               guint       position)
{
  return g_list_model_get_item (SYSPROF_MARK_CATALOG (model)->items, position);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = sysprof_mark_catalog_get_n_items;
  iface->get_item_type = sysprof_mark_catalog_get_item_type;
  iface->get_item = sysprof_mark_catalog_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMarkCatalog, sysprof_mark_catalog, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_mark_catalog_dispose (GObject *object)
{
  SysprofMarkCatalog *self = (SysprofMarkCatalog *)object;

  g_clear_pointer (&self->name, g_free);
  g_clear_object (&self->items);

  G_OBJECT_CLASS (sysprof_mark_catalog_parent_class)->dispose (object);
}

static void
sysprof_mark_catalog_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofMarkCatalog *self = SYSPROF_MARK_CATALOG (object);

  switch (prop_id)
    {
    case PROP_KIND:
      g_value_set_enum (value, sysprof_mark_catalog_get_kind (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, sysprof_mark_catalog_get_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_catalog_class_init (SysprofMarkCatalogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_mark_catalog_dispose;
  object_class->get_property = sysprof_mark_catalog_get_property;

  properties[PROP_KIND] =
    g_param_spec_enum ("kind", NULL, NULL,
                       SYSPROF_TYPE_MARK_CATALOG_KIND,
                       SYSPROF_MARK_CATALOG_KIND_GROUP,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_catalog_init (SysprofMarkCatalog *self)
{
}

const char *
sysprof_mark_catalog_get_name (SysprofMarkCatalog *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (self), NULL);

  return self->name;
}

SysprofMarkCatalog *
_sysprof_mark_catalog_new (const char             *name,
                           GListModel             *items,
                           SysprofMarkCatalogKind  kind)
{
  SysprofMarkCatalog *self;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (G_IS_LIST_MODEL (items), NULL);

  self = g_object_new (SYSPROF_TYPE_MARK_CATALOG, NULL);
  self->name = g_strdup (name);
  self->items = g_object_ref (items);
  self->kind = kind;

  return self;
}

SysprofMarkCatalogKind
sysprof_mark_catalog_get_kind (SysprofMarkCatalog *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (self), 0);

  return self->kind;
}

G_DEFINE_ENUM_TYPE (SysprofMarkCatalogKind, sysprof_mark_catalog_kind,
                    G_DEFINE_ENUM_VALUE (SYSPROF_MARK_CATALOG_KIND_GROUP, "group"),
                    G_DEFINE_ENUM_VALUE (SYSPROF_MARK_CATALOG_KIND_NAME, "name"))
