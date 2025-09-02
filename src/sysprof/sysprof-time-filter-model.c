/* sysprof-time-filter-model.c
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

#include <gtk/gtk.h>

#include "sysprof-time-filter-model.h"

struct _SysprofTimeFilterModel
{
  GObject            parent_instance;
  GtkExpression     *expression;
  GtkSliceListModel *slice;
  SysprofTimeSpan    time_span;
  GSignalGroup      *signal_group;
  guint              inclusive : 1;
};

enum {
  PROP_0,
  PROP_EXPRESSION,
  PROP_INCLUSIVE,
  PROP_MODEL,
  PROP_TIME_SPAN,
  N_PROPS
};

static void sysprof_time_filter_model_update (SysprofTimeFilterModel *self);

static GType
sysprof_time_filter_model_get_item_type (GListModel *model)
{
  return g_list_model_get_item_type (G_LIST_MODEL (SYSPROF_TIME_FILTER_MODEL (model)->slice));
}

static guint
sysprof_time_filter_model_get_n_items (GListModel *model)
{
  return g_list_model_get_n_items (G_LIST_MODEL (SYSPROF_TIME_FILTER_MODEL (model)->slice));
}

static gpointer
sysprof_time_filter_model_get_item (GListModel *model,
                                    guint       position)
{
  return g_list_model_get_item (G_LIST_MODEL (SYSPROF_TIME_FILTER_MODEL (model)->slice), position);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_time_filter_model_get_item_type;
  iface->get_n_items = sysprof_time_filter_model_get_n_items;
  iface->get_item = sysprof_time_filter_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofTimeFilterModel, sysprof_time_filter_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static inline gint64
get_item_time (SysprofTimeFilterModel *self,
               GObject                *item)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT64);
  gtk_expression_evaluate (self->expression, item, &value);
  return g_value_get_int64 (&value);
}

static void
sysprof_time_filter_model_finalize (GObject *object)
{
  SysprofTimeFilterModel *self = (SysprofTimeFilterModel *)object;

  g_clear_object (&self->signal_group);
  g_clear_object (&self->slice);
  g_clear_pointer (&self->expression, gtk_expression_unref);

  G_OBJECT_CLASS (sysprof_time_filter_model_parent_class)->finalize (object);
}

static void
sysprof_time_filter_model_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofTimeFilterModel *self = SYSPROF_TIME_FILTER_MODEL (object);

  switch (prop_id)
    {
    case PROP_EXPRESSION:
      gtk_value_set_expression (value, sysprof_time_filter_model_get_expression (self));
      break;

    case PROP_INCLUSIVE:
      g_value_set_boolean (value, self->inclusive);
      break;

    case PROP_MODEL:
      g_value_set_object (value, sysprof_time_filter_model_get_model (self));
      break;

    case PROP_TIME_SPAN:
      g_value_set_boxed (value, sysprof_time_filter_model_get_time_span (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_filter_model_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SysprofTimeFilterModel *self = SYSPROF_TIME_FILTER_MODEL (object);

  switch (prop_id)
    {
    case PROP_EXPRESSION:
      sysprof_time_filter_model_set_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_INCLUSIVE:
      self->inclusive = g_value_get_boolean (value);
      sysprof_time_filter_model_update (self);
      break;

    case PROP_MODEL:
      sysprof_time_filter_model_set_model (self, g_value_get_object (value));
      break;

    case PROP_TIME_SPAN:
      sysprof_time_filter_model_set_time_span (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_filter_model_class_init (SysprofTimeFilterModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_time_filter_model_finalize;
  object_class->get_property = sysprof_time_filter_model_get_property;
  object_class->set_property = sysprof_time_filter_model_set_property;

  properties[PROP_EXPRESSION] =
    gtk_param_spec_expression ("expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofTimeFilterModel:inclusive:
   *
   * If we're inclusive of items that are off the edge of the
   * boundaries.
   */
  properties[PROP_INCLUSIVE] =
    g_param_spec_boolean ("inclusive", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TIME_SPAN] =
    g_param_spec_boxed ("time-span", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_time_filter_model_init (SysprofTimeFilterModel *self)
{
  self->signal_group = g_signal_group_new (G_TYPE_LIST_MODEL);
  g_signal_group_connect_swapped (self->signal_group,
                                  "items-changed",
                                  G_CALLBACK (sysprof_time_filter_model_update),
                                  self);

  self->expression = gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_FRAME, NULL, "time");
  self->inclusive = TRUE;

  self->time_span.begin_nsec = G_MININT64;
  self->time_span.end_nsec = G_MAXINT64;

  self->slice = gtk_slice_list_model_new (NULL, 0, GTK_INVALID_LIST_POSITION);
  g_signal_connect_object (self->slice,
                           "items-changed",
                           G_CALLBACK (g_list_model_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
}

static inline void
uint_order (guint *a,
            guint *b)
{
  if (*a > *b)
    {
      guint what_was_a = *a;
      *a = *b;
      *b = what_was_a;
    }
}

static guint
binary_search_lte (SysprofTimeFilterModel *self,
                   GListModel             *model,
                   gint64                  value)
{
  g_autoptr(GObject) first = NULL;
  g_autoptr(GObject) last = NULL;
  guint lo;
  guint hi;
  guint n_items;

  g_assert (G_IS_LIST_MODEL (model));
  g_assert (g_list_model_get_n_items (model) > 0);

  n_items = g_list_model_get_n_items (model);
  first = g_list_model_get_item (model, 0);
  last = g_list_model_get_item (model, n_items - 1);

  if (value < get_item_time (self, first))
    return GTK_INVALID_LIST_POSITION;
  else if (value == get_item_time (self, first))
    return 0;
  else if (value >= get_item_time (self, last))
    return n_items - 1;

  g_clear_object (&first);
  g_clear_object (&last);

  lo = 0;
  hi = n_items - 1;

  while (lo <= hi)
    {
      guint mid = lo + (hi - lo) / 2;
      g_autoptr(GObject) item = g_list_model_get_item (model, mid);

      if (value < get_item_time (self, item))
        hi = mid - 1;
      else if (value > get_item_time (self, item))
        lo = mid + 1;
      else
        return mid;
    }

  uint_order (&lo, &hi);

  first = g_list_model_get_item (model, lo);
  last = g_list_model_get_item (model, hi);

  if (value < get_item_time (self, first))
    return lo - 1;
  else if (value > get_item_time (self, first))
    return lo;
  else if (value < get_item_time (self, last))
    return hi;

  return GTK_INVALID_LIST_POSITION;
}

static guint
binary_search_gte (SysprofTimeFilterModel *self,
                   GListModel             *model,
                   gint64                  value)
{
  g_autoptr(GObject) first = NULL;
  g_autoptr(GObject) last = NULL;
  guint lo;
  guint hi;
  guint n_items;

  g_assert (G_IS_LIST_MODEL (model));
  g_assert (g_list_model_get_n_items (model) > 0);

  n_items = g_list_model_get_n_items (model);
  first = g_list_model_get_item (model, 0);
  last = g_list_model_get_item (model, n_items - 1);

  if (value <= get_item_time (self, first))
    return 0;
  else if (value == get_item_time (self, last))
    return n_items - 1;
  else if (value > get_item_time (self, last))
    return GTK_INVALID_LIST_POSITION;

  g_clear_object (&first);
  g_clear_object (&last);

  lo = 0;
  hi = n_items - 1;

  while (lo <= hi)
    {
      guint mid = lo + (hi - lo) / 2;
      g_autoptr(GObject) item = g_list_model_get_item (model, mid);

      if (value < get_item_time (self, item))
        hi = mid - 1;
      else if (value > get_item_time (self, item))
        lo = mid + 1;
      else
        return mid;
    }

  uint_order (&lo, &hi);

  first = g_list_model_get_item (model, lo);
  last = g_list_model_get_item (model, hi);

  if (value > get_item_time (self, first))
    return lo + 1;
  else if (value < get_item_time (self, last))
    return hi - 1;

  return GTK_INVALID_LIST_POSITION;
}

static void
calculate_bounds (SysprofTimeFilterModel *self,
                  GListModel             *model,
                  const SysprofTimeSpan  *time_span,
                  guint                  *offset,
                  guint                  *size)
{
  guint begin;
  guint end;

  g_assert (SYSPROF_IS_TIME_FILTER_MODEL (self));
  g_assert (!model || G_IS_LIST_MODEL (model));
  g_assert (time_span != NULL);
  g_assert (offset != NULL);
  g_assert (size != NULL);

  *offset = 0;
  *size = GTK_INVALID_LIST_POSITION-1;

  if (model == NULL || g_list_model_get_n_items (model) == 0)
    return;

  if (time_span->begin_nsec == G_MININT64 && time_span->end_nsec == G_MAXINT64)
    return;

  begin = binary_search_gte (self, model, time_span->begin_nsec);
  end = binary_search_lte (self, model, time_span->end_nsec);

  if (begin > end || begin == GTK_INVALID_LIST_POSITION)
    return;

  *offset = begin;

  if (end != GTK_INVALID_LIST_POSITION)
    *size = end - begin + 1;
}

static void
sysprof_time_filter_model_update (SysprofTimeFilterModel *self)
{
  GListModel *model;
  guint offset;
  guint size;

  g_assert (SYSPROF_IS_TIME_FILTER_MODEL (self));

  model = sysprof_time_filter_model_get_model (self);

  calculate_bounds (self, model, &self->time_span, &offset, &size);

  /* We want to extend our selection to adjacent values so that we
   * are more likely to include enough data to overlap the boundaries
   * when drawing graphs.
   *
   * This can sort of muck up callgraphs, so it's disabled in those.
   */
  if (self->inclusive)
    {
      if (offset > 0)
        offset--;
      size++;
    }

  gtk_slice_list_model_set_offset (self->slice, offset);
  gtk_slice_list_model_set_size (self->slice, size);
}

SysprofTimeFilterModel *
sysprof_time_filter_model_new (GListModel            *model,
                               const SysprofTimeSpan *time_span)
{
  SysprofTimeFilterModel *self;

  g_return_val_if_fail (!model || G_IS_LIST_MODEL (model), NULL);

  self = g_object_new (SYSPROF_TYPE_TIME_FILTER_MODEL,
                       "model", model,
                       "time-span", time_span,
                       NULL);

  g_clear_object (&model);

  return self;
}

GListModel *
sysprof_time_filter_model_get_model (SysprofTimeFilterModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self), NULL);

  return gtk_slice_list_model_get_model (self->slice);
}

void
sysprof_time_filter_model_set_model (SysprofTimeFilterModel *self,
                                     GListModel             *model)
{
  g_return_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (model == sysprof_time_filter_model_get_model (self))
    return;

  g_signal_group_set_target (self->signal_group, model);
  gtk_slice_list_model_set_model (self->slice, model);
  sysprof_time_filter_model_update (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

const SysprofTimeSpan *
sysprof_time_filter_model_get_time_span (SysprofTimeFilterModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self), NULL);

  return &self->time_span;
}

void
sysprof_time_filter_model_set_time_span (SysprofTimeFilterModel *self,
                                         const SysprofTimeSpan  *time_span)
{
  SysprofTimeSpan empty = {G_MININT64, G_MAXINT64};

  g_return_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self));

  if (time_span == NULL)
    time_span = &empty;

  if (!sysprof_time_span_equal (&self->time_span, time_span))
    {
      self->time_span = *time_span;
      sysprof_time_filter_model_update (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_SPAN]);
    }
}

GtkExpression *
sysprof_time_filter_model_get_expression (SysprofTimeFilterModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self), NULL);

  return self->expression;
}

void
sysprof_time_filter_model_set_expression (SysprofTimeFilterModel *self,
                                          GtkExpression          *expression)
{
  g_return_if_fail (SYSPROF_IS_TIME_FILTER_MODEL (self));
  g_return_if_fail (!expression || GTK_IS_EXPRESSION (expression));

  if (expression == self->expression)
    return;

  if (expression)
    gtk_expression_ref (expression);

  g_clear_pointer (&self->expression, gtk_expression_unref);

  if (expression == NULL)
    self->expression = gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_FRAME, NULL, "time");
  else
    self->expression = expression;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPRESSION]);
}
