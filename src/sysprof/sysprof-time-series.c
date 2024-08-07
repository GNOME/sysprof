/* sysprof-time-series.c
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
#include "sysprof-time-series.h"
#include "sysprof-time-series-item-private.h"

struct _SysprofTimeSeries
{
  SysprofSeries  parent_instance;
  GtkExpression *begin_time_expression;
  GtkExpression *end_time_expression;
  GtkExpression *label_expression;
};

struct _SysprofTimeSeriesClass
{
  SysprofSeriesClass parent_instance;
};

enum {
  PROP_0,
  PROP_BEGIN_TIME_EXPRESSION,
  PROP_END_TIME_EXPRESSION,
  PROP_LABEL_EXPRESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTimeSeries, sysprof_time_series, SYSPROF_TYPE_SERIES)

static GParamSpec *properties [N_PROPS];

static gpointer
sysprof_time_series_get_series_item (SysprofSeries *series,
                                     guint          position,
                                     gpointer       item)
{
  SysprofTimeSeries *self = SYSPROF_TIME_SERIES (series);

  return _sysprof_time_series_item_new (item,
                                        gtk_expression_ref (self->begin_time_expression),
                                        gtk_expression_ref (self->end_time_expression));
}

static void
sysprof_time_series_finalize (GObject *object)
{
  SysprofTimeSeries *self = (SysprofTimeSeries *)object;

  g_clear_pointer (&self->end_time_expression, gtk_expression_unref);
  g_clear_pointer (&self->label_expression, gtk_expression_unref);
  g_clear_pointer (&self->begin_time_expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_time_series_parent_class)->finalize (object);
}

static void
sysprof_time_series_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofTimeSeries *self = SYSPROF_TIME_SERIES (object);

  switch (prop_id)
    {
      case PROP_BEGIN_TIME_EXPRESSION:
      gtk_value_set_expression (value, self->begin_time_expression);
      break;

    case PROP_END_TIME_EXPRESSION:
      gtk_value_set_expression (value, self->end_time_expression);
      break;

    case PROP_LABEL_EXPRESSION:
      gtk_value_set_expression (value, self->label_expression);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_series_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofTimeSeries *self = SYSPROF_TIME_SERIES (object);

  switch (prop_id)
    {
    case PROP_BEGIN_TIME_EXPRESSION:
      sysprof_time_series_set_begin_time_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_END_TIME_EXPRESSION:
      sysprof_time_series_set_end_time_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_LABEL_EXPRESSION:
      sysprof_time_series_set_label_expression (self, gtk_value_get_expression (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_series_class_init (SysprofTimeSeriesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSeriesClass *series_class = SYSPROF_SERIES_CLASS (klass);

  object_class->finalize = sysprof_time_series_finalize;
  object_class->get_property = sysprof_time_series_get_property;
  object_class->set_property = sysprof_time_series_set_property;

  series_class->series_item_type = SYSPROF_TYPE_TIME_SERIES_ITEM;
  series_class->get_series_item = sysprof_time_series_get_series_item;

  properties [PROP_BEGIN_TIME_EXPRESSION] =
    gtk_param_spec_expression ("begin-time-expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME_EXPRESSION] =
    gtk_param_spec_expression ("end-time-expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_LABEL_EXPRESSION] =
    gtk_param_spec_expression ("label-expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_time_series_init (SysprofTimeSeries *self)
{
}

/**
 * sysprof_time_series_new:
 * @title: (nullable): a title for the series
 * @model: (transfer full) (nullable): a #GListModel for the series
 * @begin_time_expression: (transfer full) (nullable): a #GtkExpression for
 *   extracting the begin time value from @model items.
 * @end_time_expression: (transfer full) (nullable): a #GtkExpression for
 *   extracting the end time value from @model items.
 * @label_expression: (transfer full) (nullable): a #GtkExpression for
 *   extracting the label from @model items.
 *
 * A #SysprofSeries which contains Time,Duration pairs.
 *
 * Returns: (transfer full): a #SysprofSeries
 */
SysprofSeries *
sysprof_time_series_new (const char    *title,
                         GListModel    *model,
                         GtkExpression *begin_time_expression,
                         GtkExpression *end_time_expression,
                         GtkExpression *label_expression)
{
  SysprofTimeSeries *xy;

  xy = g_object_new (SYSPROF_TYPE_TIME_SERIES,
                     "title", title,
                     "model", model,
                     "begin-time-expression", begin_time_expression,
                     "end-time-expression", end_time_expression,
                     "label-expression", label_expression,
                     NULL);

  g_clear_pointer (&begin_time_expression, gtk_expression_unref);
  g_clear_pointer (&end_time_expression, gtk_expression_unref);
  g_clear_pointer (&label_expression, gtk_expression_unref);
  g_clear_object (&model);

  return SYSPROF_SERIES (xy);
}

/**
 * sysprof_time_series_get_begin_time_expression:
 * @self: a #SysprofTimeSeries
 *
 * Gets the #SysprofTimeSeries:x-expression property.
 *
 * This is used to extract the X coordinate from items in the #GListModel.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_time_series_get_begin_time_expression (SysprofTimeSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES (self), NULL);

  return self->begin_time_expression;
}

void
sysprof_time_series_set_begin_time_expression (SysprofTimeSeries *self,
                                               GtkExpression     *begin_time_expression)
{
  g_return_if_fail (SYSPROF_IS_TIME_SERIES (self));

  if (self->begin_time_expression == begin_time_expression)
    return;

  if (begin_time_expression)
    gtk_expression_ref (begin_time_expression);

  g_clear_pointer (&self->begin_time_expression, gtk_expression_unref);

  self->begin_time_expression = begin_time_expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BEGIN_TIME_EXPRESSION]);
}

/**
 * sysprof_time_series_get_end_time_expression:
 * @self: a #SysprofTimeSeries
 *
 * Gets the #SysprofTimeSeries:y-expression property.
 *
 * This is used to extract the Y coordinate from items in the #GListModel.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_time_series_get_end_time_expression (SysprofTimeSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES (self), NULL);

  return self->end_time_expression;
}

void
sysprof_time_series_set_end_time_expression (SysprofTimeSeries *self,
                                             GtkExpression     *end_time_expression)
{
  g_return_if_fail (SYSPROF_IS_TIME_SERIES (self));

  if (self->end_time_expression == end_time_expression)
    return;

  if (end_time_expression)
    gtk_expression_ref (end_time_expression);

  g_clear_pointer (&self->end_time_expression, gtk_expression_unref);

  self->end_time_expression = end_time_expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END_TIME_EXPRESSION]);
}

/**
 * sysprof_time_series_get_label_expression:
 * @self: a #SysprofTimeSeries
 *
 * Gets the #SysprofTimeSeries:y-expression property.
 *
 * This is used to extract the Y coordinate from items in the #GListModel.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_time_series_get_label_expression (SysprofTimeSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES (self), NULL);

  return self->label_expression;
}

void
sysprof_time_series_set_label_expression (SysprofTimeSeries *self,
                                          GtkExpression     *label_expression)
{
  g_return_if_fail (SYSPROF_IS_TIME_SERIES (self));

  if (self->label_expression == label_expression)
    return;

  if (label_expression)
    gtk_expression_ref (label_expression);

  g_clear_pointer (&self->label_expression, gtk_expression_unref);

  self->label_expression = label_expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LABEL_EXPRESSION]);
}

char *
sysprof_time_series_dup_label (SysprofTimeSeries *self,
                               guint              position)
{
  g_autoptr(GObject) item = NULL;
  g_auto(GValue) value = G_VALUE_INIT;
  GListModel *model;

  g_return_val_if_fail (SYSPROF_IS_TIME_SERIES (self), NULL);

  if (self->label_expression == NULL)
    return NULL;

  if (!(model = sysprof_series_get_model (SYSPROF_SERIES (self))))
    return NULL;

  if (!(item = g_list_model_get_item (model, position)))
    return NULL;

  gtk_expression_evaluate (self->label_expression, item, &value);
  return g_value_dup_string (&value);
}
