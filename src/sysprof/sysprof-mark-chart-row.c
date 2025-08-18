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

  GMenuModel           *context_menu;
  GtkWidget            *context_menu_popover;

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

static double
opacity_from_item_visibility (gpointer unused,
                              gboolean visible)
{
  return visible ? 1.0 : 0.33;
}

static void
filter_by_mark_action (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *parameter)
{
  SysprofMarkChartRow *self = (SysprofMarkChartRow *)widget;
  SysprofMarkCatalog *catalog;
  SysprofSession *session;

  g_assert (SYSPROF_IS_MARK_CHART_ROW (self));

  if (self->item == NULL ||
      !(session = sysprof_mark_chart_item_get_session (self->item)))
    return;

  catalog = sysprof_mark_chart_item_get_catalog (self->item);
  g_assert (SYSPROF_IS_MARK_CATALOG (catalog));

  sysprof_session_filter_by_mark (session, catalog);
}

static void
toggle_catalog_action (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *parameter)
{
  SysprofMarkChartRow *self = (SysprofMarkChartRow *)widget;
  SysprofMarksSectionModelItem *item;

  g_assert (SYSPROF_IS_MARK_CHART_ROW (self));

  if (self->item == NULL)
    return;

  item = sysprof_mark_chart_item_get_model_item (self->item);
  sysprof_marks_section_model_item_set_visible (item, !sysprof_marks_section_model_item_get_visible (item));
}

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
sysprof_mark_chart_row_button_pressed_cb (SysprofMarkChartRow *self,
                                          int                  n_press,
                                          double               x,
                                          double               y,
                                          GtkGestureClick     *gesture)
{
  GdkEventSequence *current;
  GdkEvent *event;

  g_assert (SYSPROF_IS_MARK_CHART_ROW (self));
  g_assert (GTK_IS_GESTURE_CLICK (gesture));

  current = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (gesture));
  event = gtk_gesture_get_last_event (GTK_GESTURE (gesture), current);

  if (!gdk_event_triggers_context_menu (event))
    return;

  if (!self->context_menu_popover)
    {
      self->context_menu_popover = gtk_popover_menu_new_from_model (self->context_menu);
      gtk_popover_set_position (GTK_POPOVER (self->context_menu_popover), GTK_POS_BOTTOM);
      gtk_popover_set_has_arrow (GTK_POPOVER (self->context_menu_popover), FALSE);
      gtk_widget_set_halign (self->context_menu_popover, GTK_ALIGN_START);
      gtk_widget_set_parent (GTK_WIDGET (self->context_menu_popover), GTK_WIDGET (self));
    }

  if (x != -1 && y != -1)
    {
      GdkRectangle rect = { x, y, 1, 1 };
      gtk_popover_set_pointing_to (GTK_POPOVER (self->context_menu_popover), &rect);
    }
  else
    {
      gtk_popover_set_pointing_to (GTK_POPOVER (self->context_menu_popover), NULL);
    }

  gtk_popover_popup (GTK_POPOVER (self->context_menu_popover));

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
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
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChartRow, context_menu);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChartRow, layer);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_mark_chart_row_activate_layer_item_cb);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_mark_chart_row_button_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, opacity_from_item_visibility);

  gtk_widget_class_install_action (widget_class, "markchartrow.toggle-catalog", NULL, toggle_catalog_action);
  gtk_widget_class_install_action (widget_class, "markchartrow.filter-by-mark", NULL, filter_by_mark_action);

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
