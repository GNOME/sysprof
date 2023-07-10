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
  GtkExpression *begin_time_expression;
  GtkExpression *end_time_expression;
  GObject *item;
};

enum {
  PROP_0,
  PROP_DURATION,
  PROP_ITEM,
  PROP_BEGIN_TIME,
  PROP_END_TIME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTimeSeriesItem, sysprof_time_series_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_time_series_item_finalize (GObject *object)
{
  SysprofTimeSeriesItem *self = (SysprofTimeSeriesItem *)object;

  g_clear_object (&self->item);
  g_clear_pointer (&self->begin_time_expression, gtk_expression_unref);
  g_clear_pointer (&self->end_time_expression, gtk_expression_unref);

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

    case PROP_DURATION:
      g_value_set_int64 (value, sysprof_time_series_item_get_duration (self));
      break;

    case PROP_BEGIN_TIME:
      g_value_set_int64 (value, sysprof_time_series_item_get_begin_time (self));
      break;

    case PROP_END_TIME:
      g_value_set_int64 (value, sysprof_time_series_item_get_end_time (self));
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

  properties [PROP_BEGIN_TIME] =
    g_param_spec_int64 ("begin-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DURATION] =
    g_param_spec_int64 ("duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_time_series_item_init (SysprofTimeSeriesItem *self)
{
}

SysprofTimeSeriesItem *
_sysprof_time_series_item_new (GObject       *item,
                               GtkExpression *begin_time_expression,
                               GtkExpression *end_time_expression)
{
  SysprofTimeSeriesItem *self;

  self = g_object_new (SYSPROF_TYPE_TIME_SERIES_ITEM, NULL);
  self->item = item;
  self->begin_time_expression = begin_time_expression;
  self->end_time_expression = end_time_expression;

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

gint64
sysprof_time_series_item_get_begin_time (SysprofTimeSeriesItem *self)
{
  GValue value = G_VALUE_INIT;
  gint64 ret;

  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES_ITEM (self), 0);

  g_value_init (&value, G_TYPE_INT64);
  gtk_expression_evaluate (self->begin_time_expression, self->item, &value);
  ret = g_value_get_int64 (&value);
  g_value_unset (&value);

  return ret;
}

gint64
sysprof_time_series_item_get_duration (SysprofTimeSeriesItem *self)
{
  return sysprof_time_series_item_get_end_time (self) - sysprof_time_series_item_get_begin_time (self);
}

gint64
sysprof_time_series_item_get_end_time (SysprofTimeSeriesItem *self)
{
  GValue value = G_VALUE_INIT;
  gint64 ret;

  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES_ITEM (self), 0);

  g_value_init (&value, G_TYPE_INT64);
  gtk_expression_evaluate (self->end_time_expression, self->item, &value);
  ret = g_value_get_int64 (&value);
  g_value_unset (&value);

  return ret;
}
