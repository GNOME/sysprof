/* sysprof-mark-chart-item.c
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

#include "sysprof-mark-chart-item-private.h"
#include "sysprof-marks-section-model-item.h"
#include "sysprof-sampled-model.h"
#include "sysprof-time-filter-model.h"

struct _SysprofMarkChartItem
{
  GObject                       parent_instance;
  SysprofSession               *session;
  SysprofMarksSectionModelItem *item;
  SysprofMarkCatalog           *catalog;
  SysprofTimeFilterModel       *filtered;
  SysprofSampledModel          *sampled;
  SysprofSeries                *series;
  guint                         max_items;
};

enum {
  PROP_0,
  PROP_ITEM,
  PROP_MAX_ITEMS,
  PROP_SESSION,
  PROP_CATALOG,
  PROP_SERIES,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChartItem, sysprof_mark_chart_item, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
sysprof_mark_chart_item_constructed (GObject *object)
{
  SysprofMarkChartItem *self = (SysprofMarkChartItem *)object;

  G_OBJECT_CLASS (sysprof_mark_chart_item_parent_class)->constructed (object);

  if (self->item == NULL || self->session == NULL)
    g_return_if_reached ();

  self->catalog = sysprof_marks_section_model_item_get_item (self->item);
  g_assert (SYSPROF_IS_MARK_CATALOG (self->catalog));

  sysprof_time_filter_model_set_model (self->filtered, G_LIST_MODEL (self->catalog));

  g_object_bind_property (self->session, "selected-time",
                          self->filtered, "time-span",
                          G_BINDING_SYNC_CREATE);
}

static void
sysprof_mark_chart_item_dispose (GObject *object)
{
  SysprofMarkChartItem *self = (SysprofMarkChartItem *)object;

  g_clear_object (&self->session);
  g_clear_object (&self->item);
  g_clear_object (&self->filtered);
  g_clear_object (&self->sampled);
  g_clear_object (&self->series);

  G_OBJECT_CLASS (sysprof_mark_chart_item_parent_class)->dispose (object);
}

static void
sysprof_mark_chart_item_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofMarkChartItem *self = SYSPROF_MARK_CHART_ITEM (object);

  switch (prop_id)
    {
    case PROP_CATALOG:
      g_value_set_object (value, sysprof_mark_chart_item_get_catalog (self));
      break;

    case PROP_ITEM:
      g_value_set_object (value, sysprof_mark_chart_item_get_model_item (self));
      break;

    case PROP_MAX_ITEMS:
      g_value_set_uint (value, self->max_items);
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_mark_chart_item_get_session (self));
      break;

    case PROP_SERIES:
      g_value_set_object (value, sysprof_mark_chart_item_get_series (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_item_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofMarkChartItem *self = SYSPROF_MARK_CHART_ITEM (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      self->item = g_value_dup_object (value);
      break;

    case PROP_SESSION:
      self->session = g_value_dup_object (value);
      break;

    case PROP_MAX_ITEMS:
      self->max_items = g_value_get_uint (value);
      sysprof_sampled_model_set_max_items (self->sampled, self->max_items);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_item_class_init (SysprofMarkChartItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = sysprof_mark_chart_item_constructed;
  object_class->dispose = sysprof_mark_chart_item_dispose;
  object_class->get_property = sysprof_mark_chart_item_get_property;
  object_class->set_property = sysprof_mark_chart_item_set_property;

  properties[PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_CATALOG] =
    g_param_spec_object ("catalog", NULL, NULL,
                         SYSPROF_TYPE_MARK_CATALOG,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MAX_ITEMS] =
    g_param_spec_uint ("max-items", NULL, NULL,
                       1, G_MAXUINT-1, 1000,
                       (G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIES] =
    g_param_spec_object ("series", NULL, NULL,
                         SYSPROF_TYPE_TIME_SERIES,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_chart_item_init (SysprofMarkChartItem *self)
{
  self->max_items = 1000;

  self->filtered = sysprof_time_filter_model_new (NULL, NULL);
  self->sampled = sysprof_sampled_model_new (g_object_ref (G_LIST_MODEL (self->filtered)),
                                             self->max_items);

  self->series = sysprof_time_series_new (NULL,
                                          g_object_ref (G_LIST_MODEL (self->sampled)),
                                          gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "time"),
                                          gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "end-time"),
                                          gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "message"));
}

SysprofMarkChartItem *
sysprof_mark_chart_item_new (SysprofSession               *session,
                             SysprofMarksSectionModelItem *item)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (session), NULL);
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL_ITEM (item), NULL);

  return g_object_new (SYSPROF_TYPE_MARK_CHART_ITEM,
                       "session", session,
                       "item", item,
                       NULL);
}

SysprofMarksSectionModelItem *
sysprof_mark_chart_item_get_model_item (SysprofMarkChartItem *self)
{
  return self->item;
}

SysprofMarkCatalog *
sysprof_mark_chart_item_get_catalog (SysprofMarkChartItem *self)
{
  return self->catalog;
}

SysprofSession *
sysprof_mark_chart_item_get_session (SysprofMarkChartItem *self)
{
  return self->session;
}

SysprofTimeSeries *
sysprof_mark_chart_item_get_series (SysprofMarkChartItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART_ITEM (self), NULL);

  return SYSPROF_TIME_SERIES (self->series);
}
