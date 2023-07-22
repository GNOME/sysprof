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
  char *group;
  char *name;
  gint64 min_duration;
  gint64 max_duration;
  gint64 avg_duration;
  gint64 med_duration;
} SysprofMarkCatalogPrivate;

enum {
  PROP_0,
  PROP_GROUP,
  PROP_NAME,
  PROP_MIN_DURATION,
  PROP_MAX_DURATION,
  PROP_AVERAGE_DURATION,
  PROP_MEDIAN_DURATION,
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

  g_clear_pointer (&self->group, g_free);
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
    case PROP_GROUP:
      g_value_set_string (value, sysprof_mark_catalog_get_group (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, sysprof_mark_catalog_get_name (self));
      break;

    case PROP_MIN_DURATION:
      g_value_set_int64 (value, sysprof_mark_catalog_get_min_duration (self));
      break;

    case PROP_MAX_DURATION:
      g_value_set_int64 (value, sysprof_mark_catalog_get_max_duration (self));
      break;

    case PROP_AVERAGE_DURATION:
      g_value_set_int64 (value, sysprof_mark_catalog_get_average_duration (self));
      break;

    case PROP_MEDIAN_DURATION:
      g_value_set_int64 (value, sysprof_mark_catalog_get_median_duration (self));
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

  properties[PROP_GROUP] =
    g_param_spec_string ("group", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MIN_DURATION] =
    g_param_spec_int64 ("min-duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MAX_DURATION] =
    g_param_spec_int64 ("max-duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_AVERAGE_DURATION] =
    g_param_spec_int64 ("average-duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MEDIAN_DURATION] =
    g_param_spec_int64 ("median-duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_catalog_init (SysprofMarkCatalog *self)
{
}

const char *
sysprof_mark_catalog_get_group (SysprofMarkCatalog *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (self), NULL);

  return self->group;
}

const char *
sysprof_mark_catalog_get_name (SysprofMarkCatalog *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (self), NULL);

  return self->name;
}

SysprofMarkCatalog *
_sysprof_mark_catalog_new (const char *group,
                           const char *name,
                           GListModel *items,
                           gint64      min,
                           gint64      max,
                           gint64      avg,
                           gint64      med)
{
  SysprofMarkCatalog *self;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (G_IS_LIST_MODEL (items), NULL);

  self = g_object_new (SYSPROF_TYPE_MARK_CATALOG, NULL);
  self->group = g_strdup (group);
  self->name = g_strdup (name);
  self->items = g_object_ref (items);
  self->min_duration = min;
  self->max_duration = max;
  self->avg_duration = avg;
  self->med_duration = med;

  return self;
}

gint64
sysprof_mark_catalog_get_min_duration (SysprofMarkCatalog *self)
{
  if (self->min_duration == G_MAXINT64)
    return 0;

  return self->min_duration;
}

gint64
sysprof_mark_catalog_get_max_duration (SysprofMarkCatalog *self)
{
  if (self->max_duration == G_MININT64)
    return 0;

  return self->max_duration;
}

gint64
sysprof_mark_catalog_get_average_duration (SysprofMarkCatalog *self)
{
  return self->avg_duration;
}

gint64
sysprof_mark_catalog_get_median_duration (SysprofMarkCatalog *self)
{
  return self->med_duration;
}
