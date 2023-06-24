/* sysprof-xy-series.c
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
#include "sysprof-xy-series.h"
#include "sysprof-xy-series-item-private.h"

struct _SysprofXYSeries
{
  SysprofSeries  parent_instance;
  GtkExpression *x_expression;
  GtkExpression *y_expression;
};

struct _SysprofXYSeriesClass
{
  SysprofSeriesClass parent_instance;
};

enum {
  PROP_0,
  PROP_X_EXPRESSION,
  PROP_Y_EXPRESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofXYSeries, sysprof_xy_series, SYSPROF_TYPE_SERIES)

static GParamSpec *properties [N_PROPS];

static gpointer
sysprof_xy_series_get_series_item (SysprofSeries *series,
                                   guint          position,
                                   gpointer       item)
{
  SysprofXYSeries *self = SYSPROF_XY_SERIES (series);

  return _sysprof_xy_series_item_new (item,
                                      gtk_expression_ref (self->x_expression),
                                      gtk_expression_ref (self->y_expression));
}

static void
sysprof_xy_series_finalize (GObject *object)
{
  SysprofXYSeries *self = (SysprofXYSeries *)object;

  g_clear_pointer (&self->x_expression, gtk_expression_unref);
  g_clear_pointer (&self->y_expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_xy_series_parent_class)->finalize (object);
}

static void
sysprof_xy_series_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SysprofXYSeries *self = SYSPROF_XY_SERIES (object);

  switch (prop_id)
    {
    case PROP_X_EXPRESSION:
      gtk_value_set_expression (value, self->x_expression);
      break;

    case PROP_Y_EXPRESSION:
      gtk_value_set_expression (value, self->y_expression);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_xy_series_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SysprofXYSeries *self = SYSPROF_XY_SERIES (object);

  switch (prop_id)
    {
    case PROP_X_EXPRESSION:
      sysprof_xy_series_set_x_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_Y_EXPRESSION:
      sysprof_xy_series_set_y_expression (self, gtk_value_get_expression (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_xy_series_class_init (SysprofXYSeriesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSeriesClass *series_class = SYSPROF_SERIES_CLASS (klass);

  object_class->finalize = sysprof_xy_series_finalize;
  object_class->get_property = sysprof_xy_series_get_property;
  object_class->set_property = sysprof_xy_series_set_property;

  series_class->series_item_type = SYSPROF_TYPE_XY_SERIES_ITEM;
  series_class->get_series_item = sysprof_xy_series_get_series_item;

  properties [PROP_X_EXPRESSION] =
    gtk_param_spec_expression ("x-expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_Y_EXPRESSION] =
    gtk_param_spec_expression ("y-expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_xy_series_init (SysprofXYSeries *self)
{
}

/**
 * sysprof_xy_series_new:
 * @title: (nullable): a title for the series
 * @model: (transfer full) (nullable): a #GListModel for the series
 * @x_expression: (transfer full) (nullable): a #GtkExpression for
 *   extracting the X value from @model items.
 * @y_expression: (transfer full) (nullable): a #GtkExpression for
 *   extracting the Y value from @model items.
 *
 * A #SysprofSeries which contains X,Y value pairs.
 *
 * Returns: (transfer full): a #SysprofSeries
 */
SysprofSeries *
sysprof_xy_series_new (const char    *title,
                       GListModel    *model,
                       GtkExpression *x_expression,
                       GtkExpression *y_expression)
{
  SysprofXYSeries *xy;

  xy = g_object_new (SYSPROF_TYPE_XY_SERIES,
                     "title", title,
                     "model", model,
                     "x-expression", x_expression,
                     "y-expression", y_expression,
                     NULL);

  g_clear_pointer (&x_expression, gtk_expression_unref);
  g_clear_pointer (&y_expression, gtk_expression_unref);
  g_clear_object (&model);

  return SYSPROF_SERIES (xy);
}

/**
 * sysprof_xy_series_get_x_expression:
 * @self: a #SysprofXYSeries
 *
 * Gets the #SysprofXYSeries:x-expression property.
 *
 * This is used to extract the X coordinate from items in the #GListModel.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_xy_series_get_x_expression (SysprofXYSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_SERIES (self), NULL);

  return self->x_expression;
}

void
sysprof_xy_series_set_x_expression (SysprofXYSeries *self,
                                    GtkExpression   *x_expression)
{
  g_return_if_fail (SYSPROF_IS_XY_SERIES (self));

  if (self->x_expression == x_expression)
    return;

  if (x_expression)
    gtk_expression_ref (x_expression);

  g_clear_pointer (&self->x_expression, gtk_expression_unref);

  self->x_expression = x_expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X_EXPRESSION]);
}

/**
 * sysprof_xy_series_get_y_expression:
 * @self: a #SysprofXYSeries
 *
 * Gets the #SysprofXYSeries:y-expression property.
 *
 * This is used to extract the Y coordinate from items in the #GListModel.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_xy_series_get_y_expression (SysprofXYSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_XY_SERIES (self), NULL);

  return self->y_expression;
}

void
sysprof_xy_series_set_y_expression (SysprofXYSeries *self,
                                    GtkExpression   *y_expression)
{
  g_return_if_fail (SYSPROF_IS_XY_SERIES (self));

  if (self->y_expression == y_expression)
    return;

  if (y_expression)
    gtk_expression_ref (y_expression);

  g_clear_pointer (&self->y_expression, gtk_expression_unref);

  self->y_expression = y_expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X_EXPRESSION]);
}
