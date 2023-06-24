/* sysprof-time-series-item.c
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

#include "sysprof-time-series-item-private.h"

struct _SysprofTimeSeriesItem
{
  GObject parent_instance;
  GtkExpression *time_expression;
  GtkExpression *duration_expression;
  GObject *item;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTimeSeriesItem, sysprof_time_series_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_time_series_item_finalize (GObject *object)
{
  SysprofTimeSeriesItem *self = (SysprofTimeSeriesItem *)object;

  g_clear_object (&self->item);
  g_clear_pointer (&self->time_expression, gtk_expression_unref);
  g_clear_pointer (&self->duration_expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_time_series_item_parent_class)->finalize (object);
}

static void
sysprof_time_series_item_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofTimeSeriesItem *self = SYSPROF_TIME_SERIES_ITEM (object);

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
sysprof_time_series_item_class_init (SysprofTimeSeriesItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_time_series_item_finalize;
  object_class->get_property = sysprof_time_series_item_get_property;

  properties [PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_time_series_item_init (SysprofTimeSeriesItem *self)
{
}

SysprofTimeSeriesItem *
_sysprof_time_series_item_new (GObject       *item,
                               GtkExpression *time_expression,
                               GtkExpression *duration_expression)
{
  SysprofTimeSeriesItem *self;

  self = g_object_new (SYSPROF_TYPE_TIME_SERIES_ITEM, NULL);
  self->item = item;
  self->time_expression = time_expression;
  self->duration_expression = duration_expression;

  return self;
}

/**
 * sysprof_time_series_item_get_item:
 * @self: a #SysprofTimeSeriesItem
 *
 * Gets the item used to generate X/Y values.
 *
 * Returns: (transfer none): a #GObject
 */
gpointer
sysprof_time_series_item_get_item (SysprofTimeSeriesItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES_ITEM (self), NULL);

  return self->item;
}

void
sysprof_time_series_item_get_time_value (SysprofTimeSeriesItem *self,
                                         GValue                *time_value)
{
  g_return_if_fail (SYSPROF_IS_TIME_SERIES_ITEM (self));

  gtk_expression_evaluate (self->time_expression, self->item, time_value);
}

void
sysprof_time_series_item_get_duration_value (SysprofTimeSeriesItem *self,
                                             GValue                *duration_value)
{
  g_return_if_fail (SYSPROF_IS_TIME_SERIES_ITEM (self));

  gtk_expression_evaluate (self->duration_expression, self->item, duration_value);
}
