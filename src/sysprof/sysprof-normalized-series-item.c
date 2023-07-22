/* sysprof-normalized-series-item.c
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

#include "sysprof-normalized-series-item.h"

struct _SysprofNormalizedSeriesItem
{
  GObject parent_instance;
  GObject *item;
  double value;
};

enum {
  PROP_0,
  PROP_ITEM,
  PROP_VALUE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofNormalizedSeriesItem, sysprof_normalized_series_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_normalized_series_item_finalize (GObject *object)
{
  SysprofNormalizedSeriesItem *self = (SysprofNormalizedSeriesItem *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_normalized_series_item_parent_class)->finalize (object);
}

static void
sysprof_normalized_series_item_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  SysprofNormalizedSeriesItem *self = SYSPROF_NORMALIZED_SERIES_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, self->item);
      break;

    case PROP_VALUE:
      g_value_set_double (value, self->value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_normalized_series_item_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  SysprofNormalizedSeriesItem *self = SYSPROF_NORMALIZED_SERIES_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      break;

    case PROP_VALUE:
      self->value = g_value_get_double (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_normalized_series_item_class_init (SysprofNormalizedSeriesItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_normalized_series_item_finalize;
  object_class->get_property = sysprof_normalized_series_item_get_property;
  object_class->set_property = sysprof_normalized_series_item_set_property;

  properties [PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_VALUE] =
    g_param_spec_double ("value", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_normalized_series_item_init (SysprofNormalizedSeriesItem *self)
{
}

double
sysprof_normalized_series_item_get_value (SysprofNormalizedSeriesItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES_ITEM (self), 0);

  return self->value;
}

/**
 * sysprof_normalized_series_item_get_item:
 * @self: a #SysprofNormalizedSeriesItem
 *
 * Gets the underlying item the normalized value was calculated for.
 *
 * Returns: (transfer none) (type GObject): a #GObject
 */
gpointer
sysprof_normalized_series_item_get_item (SysprofNormalizedSeriesItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES_ITEM (self), NULL);

  return self->item;
}
