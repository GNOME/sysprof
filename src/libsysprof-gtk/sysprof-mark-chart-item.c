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

struct _SysprofMarkChartItem
{
  GObject parent_instance;
  SysprofSession *session;
  SysprofMarkCatalog *catalog;
  GtkFilterListModel *filtered;
};

enum {
  PROP_0,
  PROP_SESSION,
  PROP_CATALOG,
  PROP_MARKS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChartItem, sysprof_mark_chart_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mark_chart_item_constructed (GObject *object)
{
  SysprofMarkChartItem *self = (SysprofMarkChartItem *)object;

  self->filtered = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (self->catalog)), NULL);
  g_object_bind_property (self->session, "filter", self->filtered, "filter", G_BINDING_SYNC_CREATE);

  G_OBJECT_CLASS (sysprof_mark_chart_item_parent_class)->constructed (object);
}

static void
sysprof_mark_chart_item_dispose (GObject *object)
{
  SysprofMarkChartItem *self = (SysprofMarkChartItem *)object;

  g_clear_object (&self->session);
  g_clear_object (&self->catalog);

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

    case PROP_MARKS:
      g_value_set_object (value, sysprof_mark_chart_item_get_marks (self));
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_mark_chart_item_get_session (self));
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
    case PROP_CATALOG:
      self->catalog = g_value_dup_object (value);
      break;

    case PROP_SESSION:
      self->session = g_value_dup_object (value);
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

  properties[PROP_CATALOG] =
    g_param_spec_object ("catalog", NULL, NULL,
                         SYSPROF_TYPE_MARK_CATALOG,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MARKS] =
    g_param_spec_object ("marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_chart_item_init (SysprofMarkChartItem *self)
{
}

SysprofMarkChartItem *
sysprof_mark_chart_item_new (SysprofSession     *session,
                             SysprofMarkCatalog *catalog)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (session), NULL);
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (catalog), NULL);

  return g_object_new (SYSPROF_TYPE_MARK_CHART_ITEM,
                       "session", session,
                       "catalog", catalog,
                       NULL);
}

GListModel *
sysprof_mark_chart_item_get_marks (SysprofMarkChartItem *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART_ITEM (self), NULL);

  return G_LIST_MODEL (self->filtered);
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
