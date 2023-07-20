/* sysprof-mark-chart-row.c
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

#include <sysprof.h>

#include "sysprof-mark-chart-row-private.h"
#include "sysprof-chart.h"
#include "sysprof-time-series-item.h"
#include "sysprof-time-span-layer.h"

struct _SysprofMarkChartRow
{
  GtkWidget             parent_instance;

  SysprofMarkChartItem *item;

  SysprofChart         *chart;
  SysprofTimeSpanLayer *layer;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChartRow, sysprof_mark_chart_row, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static gboolean
sysprof_mark_chart_row_activate_layer_item_cb (SysprofMarkChartRow *self,
                                               SysprofChartLayer   *layer,
                                               SysprofDocumentMark *mark,
                                               SysprofChart        *chart)
{
  SysprofSession *session;
  SysprofTimeSpan time_span;

  g_assert (SYSPROF_IS_MARK_CHART_ROW (self));
  g_assert (SYSPROF_IS_CHART_LAYER (layer));
  g_assert (SYSPROF_IS_DOCUMENT_MARK (mark));
  g_assert (SYSPROF_IS_CHART (chart));

  if (self->item == NULL ||
      !(session = sysprof_mark_chart_item_get_session (self->item)))
    return FALSE;

  time_span.begin_nsec = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
  time_span.end_nsec = sysprof_document_mark_get_end_time (mark);

  if (sysprof_time_span_duration (time_span) > 0)
    {
      sysprof_session_select_time (session, &time_span);
      sysprof_session_zoom_to_selection (session);

      return TRUE;
    }

  return FALSE;
}

static void
sysprof_mark_chart_row_dispose (GObject *object)
{
  SysprofMarkChartRow *self = (SysprofMarkChartRow *)object;
  GtkWidget *child;

  self->chart = NULL;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->item);

  G_OBJECT_CLASS (sysprof_mark_chart_row_parent_class)->dispose (object);
}

static void
sysprof_mark_chart_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofMarkChartRow *self = SYSPROF_MARK_CHART_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, sysprof_mark_chart_row_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofMarkChartRow *self = SYSPROF_MARK_CHART_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      sysprof_mark_chart_row_set_item (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_row_class_init (SysprofMarkChartRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_mark_chart_row_dispose;
  object_class->get_property = sysprof_mark_chart_row_get_property;
  object_class->set_property = sysprof_mark_chart_row_set_property;

  properties [PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         SYSPROF_TYPE_MARK_CHART_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "markchartrow");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-mark-chart-row.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChartRow, chart);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChartRow, layer);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_mark_chart_row_activate_layer_item_cb);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_TIME_SERIES_ITEM);
  g_type_ensure (SYSPROF_TYPE_TIME_SPAN_LAYER);
}

static void
sysprof_mark_chart_row_init (SysprofMarkChartRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

SysprofMarkChartItem *
sysprof_mark_chart_row_get_item (SysprofMarkChartRow *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART_ROW (self), NULL);

  return self->item;
}

void
sysprof_mark_chart_row_set_item (SysprofMarkChartRow  *self,
                                 SysprofMarkChartItem *item)
{
  g_return_if_fail (SYSPROF_IS_MARK_CHART_ROW (self));
  g_return_if_fail (!item || SYSPROF_IS_MARK_CHART_ITEM (item));

  if (g_set_object (&self->item, item))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM]);
}
