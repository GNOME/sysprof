/* sysprof-xy-series-item.c
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

#include "sysprof-xy-series-item-private.h"

struct _SysprofXYSeriesItem
{
  GObject parent_instance;
  GtkExpression *x_expression;
  GtkExpression *y_expression;
  GObject *item;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofXYSeriesItem, sysprof_xy_series_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_xy_series_item_finalize (GObject *object)
{
  SysprofXYSeriesItem *self = (SysprofXYSeriesItem *)object;

  g_clear_object (&self->item);
  g_clear_pointer (&self->x_expression, gtk_expression_unref);
  g_clear_pointer (&self->y_expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_xy_series_item_parent_class)->finalize (object);
}

static void
sysprof_xy_series_item_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofXYSeriesItem *self = SYSPROF_XY_SERIES_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, self->item);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_xy_series_item_class_init (SysprofXYSeriesItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_xy_series_item_finalize;
  object_class->get_property = sysprof_xy_series_item_get_property;

  properties [PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_xy_series_item_init (SysprofXYSeriesItem *self)
{
}

SysprofXYSeriesItem *
_sysprof_xy_series_item_new (GObject       *item,
                             GtkExpression *x_expression,
                             GtkExpression *y_expression)
{
  SysprofXYSeriesItem *self;

  self = g_object_new (SYSPROF_TYPE_XY_SERIES_ITEM, NULL);
  self->item = item;
  self->x_expression = x_expression;
  self->y_expression = y_expression;

  return self;
}

/**
 * sysprof_xy_series_item_get_item:
 * @self: a #SysprofXYSeriesItem
 *
 * Gets the item used to generate X/Y values.
 *
 * Returns: (transfer none): a #GObject
 */
gpointer
sysprof_xy_series_item_get_item (SysprofXYSeriesItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_SERIES_ITEM (self), NULL);

  return self->item;
}

void
sysprof_xy_series_item_get_x_value (SysprofXYSeriesItem *self,
                                    GValue              *value)
{
  g_return_if_fail (SYSPROF_IS_XY_SERIES_ITEM (self));

  gtk_expression_evaluate (self->x_expression, self->item, value);
}

void
sysprof_xy_series_item_get_y_value (SysprofXYSeriesItem *self,
                                    GValue              *value)
{
  g_return_if_fail (SYSPROF_IS_XY_SERIES_ITEM (self));

  gtk_expression_evaluate (self->y_expression, self->item, value);
}
