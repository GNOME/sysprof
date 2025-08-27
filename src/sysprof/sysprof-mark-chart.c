/* sysprof-mark-chart.c
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

#include "sysprof-css-private.h"
#include "sysprof-mark-chart.h"
#include "sysprof-mark-chart-item-private.h"
#include "sysprof-mark-chart-row-private.h"
#include "sysprof-marks-section-model-item.h"

#include "sysprof-resources.h"

struct _SysprofMarkChart
{
  GtkWidget            parent_instance;

  SysprofMarksSectionModel *model;
  GtkMapListModel          *map;

  GtkWidget                *box;
  GtkListView              *list_view;

  guint                     max_items;
};

enum {
  PROP_0,
  PROP_MAX_ITEMS,
  PROP_MARKS_SECTION_MODEL,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChart, sysprof_mark_chart, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
toggle_group_button_clicked_cb (GtkButton     *button,
                                GtkListHeader *header)
{
  SysprofMarkChartItem *item = gtk_list_header_get_item (header);
  SysprofMarkCatalog *catalog = sysprof_mark_chart_item_get_catalog (item);
  gboolean visible = !GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "hidden"));
  gboolean next = !visible;

  gtk_widget_activate_action (GTK_WIDGET (button),
                              "markssection.toggle-group",
                              "(sb)",
                              sysprof_mark_catalog_get_group (catalog),
                              next);

  gtk_button_set_icon_name (button, next ? "eye-open-negative-filled-symbolic" : "eye-not-looking-symbolic");

  g_object_set_data (G_OBJECT (button), "hidden", GINT_TO_POINTER (!next));
}

static gpointer
map_func (gpointer item,
          gpointer user_data)
{
  SysprofMarkChart *self = SYSPROF_MARK_CHART (user_data);
  SysprofSession *session;
  gpointer ret;

  session = sysprof_marks_section_model_get_session (self->model);

  ret = sysprof_mark_chart_item_new (session, item);
  g_object_bind_property (self, "max-items", ret, "max-items", G_BINDING_SYNC_CREATE);
  g_object_unref (item);

  return ret;
}

static void
sysprof_mark_chart_dispose (GObject *object)
{
  SysprofMarkChart *self = (SysprofMarkChart *)object;

  g_clear_object (&self->model);

  g_clear_pointer (&self->box, gtk_widget_unparent);

  G_OBJECT_CLASS (sysprof_mark_chart_parent_class)->dispose (object);
}

static void
sysprof_mark_chart_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofMarkChart *self = SYSPROF_MARK_CHART (object);

  switch (prop_id)
    {
    case PROP_MAX_ITEMS:
      g_value_set_uint (value, self->max_items);
      break;

    case PROP_MARKS_SECTION_MODEL:
      g_value_set_object (value, sysprof_mark_chart_get_marks_section_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofMarkChart *self = SYSPROF_MARK_CHART (object);

  switch (prop_id)
    {
    case PROP_MAX_ITEMS:
      self->max_items = g_value_get_uint (value);
      break;

    case PROP_MARKS_SECTION_MODEL:
      sysprof_mark_chart_set_marks_section_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_class_init (SysprofMarkChartClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_mark_chart_dispose;
  object_class->get_property = sysprof_mark_chart_get_property;
  object_class->set_property = sysprof_mark_chart_set_property;

  properties [PROP_MAX_ITEMS] =
    g_param_spec_uint ("max-items", NULL, NULL,
                       1, G_MAXUINT-1, 1000,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MARKS_SECTION_MODEL] =
    g_param_spec_object ("marks-section-model", NULL, NULL,
                         SYSPROF_TYPE_MARKS_SECTION_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-mark-chart.ui");
  gtk_widget_class_set_css_name (widget_class, "markchart");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChart, box);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChart, list_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkChart, map);
  gtk_widget_class_bind_template_callback (widget_class, toggle_group_button_clicked_cb);

  g_resources_register (sysprof_get_resource ());

  g_type_ensure (SYSPROF_TYPE_MARK_CHART_ITEM);
  g_type_ensure (SYSPROF_TYPE_MARK_CHART_ROW);
  g_type_ensure (SYSPROF_TYPE_MARKS_SECTION_MODEL);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_MARK);
}

static void
sysprof_mark_chart_init (SysprofMarkChart *self)
{
  _sysprof_css_init ();

  self->max_items = 1000;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_map_list_model_set_map_func (self->map, map_func, self, NULL);
}

GtkWidget *
sysprof_mark_chart_new (void)
{
  return g_object_new (SYSPROF_TYPE_MARK_CHART, NULL);
}

/**
 * sysprof_mark_chart_get_marks_section_model:
 * @self: a #SysprofMarkChart
 *
 * Returns: (transfer none) (nullable): a #SysprofMarksSectionModel or %NULL
 */
SysprofMarksSectionModel *
sysprof_mark_chart_get_marks_section_model (SysprofMarkChart *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART (self), NULL);

  return self->model;
}

void
sysprof_mark_chart_set_marks_section_model (SysprofMarkChart         *self,
                                            SysprofMarksSectionModel *model)
{
  g_return_if_fail (SYSPROF_IS_MARK_CHART (self));
  g_return_if_fail (!model || SYSPROF_IS_MARKS_SECTION_MODEL (model));

  if (self->model == model)
    return;

  g_set_object (&self->model, model);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKS_SECTION_MODEL]);
}
