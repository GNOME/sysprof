/* sysprof-normalized-series.c
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

#include "eggbitset.h"

#include "sysprof-axis-private.h"
#include "sysprof-normalized-series.h"
#include "sysprof-normalized-series-item.h"
#include "sysprof-scheduler-private.h"
#include "sysprof-series-private.h"

#define SYSPROF_NORMALIZED_SERIES_STEP_TIME_USEC (1000) /* 1 msec */

struct _SysprofNormalizedSeries
{
  SysprofSeries  parent_instance;

  SysprofSeries *series;
  SysprofAxis   *axis;
  GtkExpression *expression;
  GArray        *values;
  EggBitset     *missing;

  gulong         range_changed_handler;
  gsize          scheduled_update;

  guint          disposed : 1;
  guint          inverted : 1;
};

struct _SysprofNormalizedSeriesClass
{
  SysprofSeriesClass parent_class;
};

enum {
  PROP_0,
  PROP_AXIS,
  PROP_EXPRESSION,
  PROP_INVERTED,
  PROP_SERIES,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofNormalizedSeries, sysprof_normalized_series, SYSPROF_TYPE_SERIES)

static GParamSpec *properties [N_PROPS];

static gboolean
sysprof_normalized_series_update_missing (gint64   deadline,
                                          gpointer user_data)
{
  SysprofNormalizedSeries *self = user_data;
  g_autoptr(GtkExpression) expression = NULL;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(EggBitset) bitset = NULL;
  EggBitsetIter iter;
  guint position;

  g_assert (SYSPROF_IS_NORMALIZED_SERIES (self));

  if (self->missing == NULL || self->disposed || self->axis == NULL || self->series == NULL || self->expression == NULL)
    {
      self->scheduled_update = 0;
      return G_SOURCE_REMOVE;
    }

  bitset = egg_bitset_ref (self->missing);
  model = g_object_ref (sysprof_series_get_model (self->series));
  expression = gtk_expression_ref (self->expression);

  if (egg_bitset_iter_init_first (&iter, bitset, &position))
    {
      guint count = 0;
      guint first = position;

      for (;;)
        {
          g_autoptr(GObject) item = g_list_model_get_item (model, position);
          g_auto(GValue) value = G_VALUE_INIT;
          guint next = GTK_INVALID_LIST_POSITION;
          double *fval = &g_array_index (self->values, double, position);
          gboolean expired;

          gtk_expression_evaluate (expression, item, &value);

          g_assert (self->values->len > position);

          count++;

          if (!self->inverted)
            *fval = _sysprof_axis_normalize (self->axis, &value);
          else
            *fval = 1. - _sysprof_axis_normalize (self->axis, &value);

          egg_bitset_remove (bitset, position);

          if (self->disposed)
            break;

          /* Only do expiry check every 10 items */
          expired = count % 10 == 0 && g_get_monotonic_time () >= deadline;

          if (!egg_bitset_iter_init_first (&iter, bitset, &next) ||
              next != position + 1 ||
              expired)
            {
              g_list_model_items_changed (G_LIST_MODEL (self),
                                          first,
                                          position-first+1,
                                          position-first+1);
              first = next;
            }

          if (expired || next == GTK_INVALID_LIST_POSITION)
            break;

          position = next;
        }
    }

  if (egg_bitset_is_empty (bitset))
    {
      self->scheduled_update = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
sysprof_normalized_series_maybe_update (SysprofNormalizedSeries *self)
{
  g_assert (SYSPROF_IS_NORMALIZED_SERIES (self));

  if (self->scheduled_update || self->disposed)
    return;

  if (!self->missing || egg_bitset_is_empty (self->missing))
    return;

  self->scheduled_update = sysprof_scheduler_add (sysprof_normalized_series_update_missing, self);
}

static void
sysprof_normalized_series_items_changed (SysprofSeries *series,
                                         GListModel    *model,
                                         guint          position,
                                         guint          removed,
                                         guint          added)
{
  SysprofNormalizedSeries *self = (SysprofNormalizedSeries *)series;

  g_assert (SYSPROF_IS_NORMALIZED_SERIES (self));
  g_assert (G_IS_LIST_MODEL (model));

  if (removed > 0 || added > 0)
    {
      egg_bitset_splice (self->missing, position, removed, added);

      if (added > 0)
        egg_bitset_add_range (self->missing, position, added);
    }

  if (removed > 0)
    g_array_remove_range (self->values, position, removed);

  if (added > 0)
    {
      if (position == self->values->len)
        {
          g_array_set_size (self->values, self->values->len + added);
        }
      else
        {
          static const double empty[32] = {0};
          const double *vals = empty;
          double *alloc = NULL;

          if (added > G_N_ELEMENTS (empty))
            vals = alloc = g_new0 (double, added);

          g_array_insert_vals (self->values, position, vals, added);

          g_free (alloc);
        }
    }

  SYSPROF_SERIES_CLASS (sysprof_normalized_series_parent_class)->items_changed (series, model, position, removed, added);

  sysprof_normalized_series_maybe_update (self);
}

static void
sysprof_normalized_series_invalidate (SysprofNormalizedSeries *self)
{
  guint n_items;

  g_assert (SYSPROF_IS_NORMALIZED_SERIES (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  if (n_items > 0)
    {
      egg_bitset_remove_all (self->missing);
      egg_bitset_add_range (self->missing, 0, n_items);

      g_list_model_items_changed (G_LIST_MODEL (self), 0, n_items, n_items);

      sysprof_normalized_series_maybe_update (self);
    }
}

static gpointer
sysprof_normalized_series_get_series_item (SysprofSeries *series,
                                           guint          position,
                                           gpointer       item)
{
  SysprofNormalizedSeries *self = SYSPROF_NORMALIZED_SERIES (series);
  SysprofNormalizedSeriesItem *ret;

  g_assert (SYSPROF_IS_NORMALIZED_SERIES (series));
  g_assert (G_IS_OBJECT (item));

  if (position >= self->values->len)
    {
      g_object_unref (item);
      return NULL;
    }

  ret = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES_ITEM,
                      "item", item,
                      "value", g_array_index (self->values, double, position),
                      NULL);

  g_object_unref (item);

  return ret;
}

static void
sysprof_normalized_series_dispose (GObject *object)
{
  SysprofNormalizedSeries *self = (SysprofNormalizedSeries *)object;

  self->disposed = TRUE;

  sysprof_scheduler_clear (&self->scheduled_update);
  g_clear_signal_handler (&self->range_changed_handler, self->axis);

  g_clear_object (&self->axis);
  g_clear_object (&self->series);

  g_clear_pointer (&self->expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_normalized_series_parent_class)->dispose (object);
}

static void
sysprof_normalized_series_finalize (GObject *object)
{
  SysprofNormalizedSeries *self = (SysprofNormalizedSeries *)object;

  g_clear_pointer (&self->missing, egg_bitset_unref);
  g_clear_pointer (&self->values, g_array_unref);

  G_OBJECT_CLASS (sysprof_normalized_series_parent_class)->finalize (object);
}

static void
sysprof_normalized_series_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofNormalizedSeries *self = SYSPROF_NORMALIZED_SERIES (object);

  switch (prop_id)
    {
    case PROP_AXIS:
      g_value_set_object (value, sysprof_normalized_series_get_axis (self));
      break;

    case PROP_EXPRESSION:
      gtk_value_set_expression (value, sysprof_normalized_series_get_expression (self));
      break;

    case PROP_SERIES:
      g_value_set_object (value, sysprof_normalized_series_get_series (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_normalized_series_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SysprofNormalizedSeries *self = SYSPROF_NORMALIZED_SERIES (object);

  switch (prop_id)
    {
    case PROP_AXIS:
      sysprof_normalized_series_set_axis (self, g_value_get_object (value));
      break;

    case PROP_EXPRESSION:
      sysprof_normalized_series_set_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_SERIES:
      sysprof_normalized_series_set_series (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_normalized_series_class_init (SysprofNormalizedSeriesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSeriesClass *series_class = SYSPROF_SERIES_CLASS (klass);

  object_class->dispose = sysprof_normalized_series_dispose;
  object_class->finalize = sysprof_normalized_series_finalize;
  object_class->get_property = sysprof_normalized_series_get_property;
  object_class->set_property = sysprof_normalized_series_set_property;

  series_class->series_item_type = SYSPROF_TYPE_NORMALIZED_SERIES_ITEM;
  series_class->get_series_item = sysprof_normalized_series_get_series_item;
  series_class->items_changed = sysprof_normalized_series_items_changed;

  properties [PROP_AXIS] =
    g_param_spec_object ("axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_EXPRESSION] =
    gtk_param_spec_expression ("expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_INVERTED] =
    g_param_spec_boolean ("inverted", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SERIES] =
    g_param_spec_object ("series", NULL, NULL,
                         SYSPROF_TYPE_SERIES,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_normalized_series_init (SysprofNormalizedSeries *self)
{
  self->values = g_array_new (FALSE, TRUE, sizeof (double));
  self->missing = egg_bitset_new_empty ();
}

/**
 * sysprof_normalized_series_new:
 * @series: (transfer-full) (nullable): a #SysprofSeries
 * @axis: (transfer-full) (nullable): a #SysprofAxis
 * @expression: (transfer-full) (nullable): a #GtkExpression
 *
 * Creates a new series that will normalize values from @series
 * by extracting values using @expression and determining where
 * they fall between the axis's range.
 *
 * Returns: (transfer full) (type SysprofNormalizedSeries): a #SysprofNormalizedSeries
 */
SysprofSeries *
sysprof_normalized_series_new (SysprofSeries *series,
                               SysprofAxis   *axis,
                               GtkExpression *expression)
{
  SysprofNormalizedSeries *normalized;

  g_return_val_if_fail (!series || SYSPROF_IS_SERIES (series), NULL);
  g_return_val_if_fail (!axis || SYSPROF_IS_AXIS (axis), NULL);
  g_return_val_if_fail (!expression || GTK_IS_EXPRESSION (expression), NULL);

  normalized = g_object_new (SYSPROF_TYPE_NORMALIZED_SERIES,
                             "axis", axis,
                             "series", series,
                             NULL);

  g_clear_object (&series);
  g_clear_object (&axis);
  g_clear_pointer (&expression, gtk_expression_unref);

  return SYSPROF_SERIES (normalized);
}

double
sysprof_normalized_series_get_value_at (SysprofNormalizedSeries *self,
                                        guint                    position)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self), .0f);

  if (egg_bitset_contains (self->missing, position))
    return .0;

  if (position >= self->values->len)
    return .0;

  return g_array_index (self->values, double, position);
}

/**
 * sysprof_normalized_series_get_axis:
 * @self: a #SysprofNormalizedSeries
 *
 * Gets the axis used to normalize values.
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis or %NULL
 */
SysprofAxis *
sysprof_normalized_series_get_axis (SysprofNormalizedSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self), NULL);

  return self->axis;
}

void
sysprof_normalized_series_set_axis (SysprofNormalizedSeries *self,
                                    SysprofAxis             *axis)
{
  g_return_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self));
  g_return_if_fail (!axis || SYSPROF_IS_AXIS (axis));

  if (self->axis == axis)
    return;

  if (self->axis)
    {
      g_clear_signal_handler (&self->range_changed_handler, self->axis);
      g_clear_object (&self->axis);
    }

  if (axis)
    {
      self->axis = g_object_ref (axis);
      self->range_changed_handler =
        g_signal_connect_object (axis,
                                 "range-changed",
                                 G_CALLBACK (sysprof_normalized_series_invalidate),
                                 self,
                                 G_CONNECT_SWAPPED);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AXIS]);

  sysprof_normalized_series_invalidate (self);
}

/**
 * sysprof_normalized_series_get_expression:
 * @self: a #SysprofNormalizedSeries
 *
 * Gets the expression to used to extract values from items within
 * #SysprofNormalizedSeries:series.
 *
 * Returns: (transfer none) (nullable): a #GtkExpression or %NULL
 */
GtkExpression *
sysprof_normalized_series_get_expression (SysprofNormalizedSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self), NULL);

  return self->expression;
}

void
sysprof_normalized_series_set_expression (SysprofNormalizedSeries *self,
                                          GtkExpression           *expression)
{
  g_return_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self));
  g_return_if_fail (!expression || GTK_IS_EXPRESSION (expression));

  if (expression == self->expression)
    return;

  if (expression)
    gtk_expression_ref (expression);

  g_clear_pointer (&self->expression, gtk_expression_unref);

  self->expression = expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPRESSION]);
}

/**
 * sysprof_normalized_series_get_series:
 * @self: a #SysprofNormalizedSeries
 *
 * Gets the series containing values to normalize.
 *
 * Returns: (transfer none) (nullable): a #SysprofSeries or %NULL
 */
SysprofSeries *
sysprof_normalized_series_get_series (SysprofNormalizedSeries *self)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self), NULL);

  return self->series;
}

void
sysprof_normalized_series_set_series (SysprofNormalizedSeries *self,
                                      SysprofSeries           *series)
{
  g_return_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self));
  g_return_if_fail (!series || SYSPROF_IS_SERIES (series));

  if (g_set_object (&self->series, series))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES]);
      sysprof_series_set_model (SYSPROF_SERIES (self), G_LIST_MODEL (series));
    }
}

const double *
sysprof_normalized_series_get_values (SysprofNormalizedSeries *self,
                                      guint                   *n_values)
{
  g_return_val_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self), NULL);

  if (self->values == NULL || self->values->len == 0)
    return NULL;

  *n_values = self->values->len;

  return &g_array_index (self->values, double, 0);
}

void
sysprof_normalized_series_set_inverted (SysprofNormalizedSeries *self,
                                        gboolean                 inverted)
{
  g_return_if_fail (SYSPROF_IS_NORMALIZED_SERIES (self));

  inverted = !!inverted;

  if (inverted != self->inverted)
    {
      self->inverted = inverted;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INVERTED]);
      sysprof_normalized_series_invalidate (self);
    }
}
