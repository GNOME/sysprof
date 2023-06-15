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

#include "sysprof-mark-chart-row-private.h"

struct _SysprofMarkChartRow
{
  GtkWidget parent_instance;
  SysprofMarkCatalog *catalog;
};

enum {
  PROP_0,
  PROP_CATALOG,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChartRow, sysprof_mark_chart_row, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mark_chart_row_snapshot (GtkWidget   *widget,
                                 GtkSnapshot *snapshot)
{
  SysprofMarkChartRow *self = (SysprofMarkChartRow *)widget;
  static const GdkRGBA blue = {0,0,1,1};
  static const GdkRGBA red = {1,0,0,1};
  static const GdkRGBA white = {1,1,1,1};
  GListModel *model;
  PangoLayout *layout;
  guint n_items;
  int width;
  int height;

  g_assert (SYSPROF_IS_MARK_CHART_ROW (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->catalog == NULL)
    return;

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  model = G_LIST_MODEL (self->catalog);
  n_items = g_list_model_get_n_items (model);

  layout = gtk_widget_create_pango_layout (widget, NULL);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (model, i);
      double begin, end;

      sysprof_document_mark_get_time_fraction (mark, &begin, &end);

      if (begin == end)
        {
          gtk_snapshot_save (snapshot);
          gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (begin*width, height/2));
          gtk_snapshot_rotate (snapshot, 45.f);
          gtk_snapshot_append_color (snapshot,
                                     &red,
                                     &GRAPHENE_RECT_INIT (-5, -5, 10, 10));
          gtk_snapshot_restore (snapshot);
        }
      else
        {
          const char *message = sysprof_document_mark_get_message (mark);

          gtk_snapshot_append_color (snapshot,
                                     &blue,
                                     &GRAPHENE_RECT_INIT (begin*width, 0, end*width, height));

          if (message)
            {
              pango_layout_set_text (layout, message, -1);

              gtk_snapshot_save (snapshot);
              gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (begin*width, 0, end*width, height));
              gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (begin*width, 0));
              gtk_snapshot_append_layout (snapshot, layout, &white);
              gtk_snapshot_pop (snapshot);
              gtk_snapshot_restore (snapshot);
            }
        }
    }

  g_object_unref (layout);
}

static void
sysprof_mark_chart_row_dispose (GObject *object)
{
  SysprofMarkChartRow *self = (SysprofMarkChartRow *)object;

  g_clear_object (&self->catalog);

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
    case PROP_CATALOG:
      g_value_set_object (value, sysprof_mark_chart_row_get_catalog (self));
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
    case PROP_CATALOG:
      sysprof_mark_chart_row_set_catalog (self, g_value_get_object (value));
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

  widget_class->snapshot = sysprof_mark_chart_row_snapshot;

  properties [PROP_CATALOG] =
    g_param_spec_object ("catalog", NULL, NULL,
                         SYSPROF_TYPE_MARK_CATALOG,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_chart_row_init (SysprofMarkChartRow *self)
{
}

SysprofMarkCatalog *
sysprof_mark_chart_row_get_catalog (SysprofMarkChartRow *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART_ROW (self), NULL);

  return self->catalog;
}

void
sysprof_mark_chart_row_set_catalog (SysprofMarkChartRow *self,
                                    SysprofMarkCatalog  *catalog)
{
  g_return_if_fail (SYSPROF_IS_MARK_CHART_ROW (self));
  g_return_if_fail (!catalog || SYSPROF_IS_MARK_CATALOG (catalog));

  if (g_set_object (&self->catalog, catalog))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATALOG]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
