/*
 * sysprof-session-filters-widget.c
 *
 * Copyright 2025 Georges Basile Stavracas Neto <feaneron@igalia.com>
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

#include "sysprof-mark-filter.h"
#include "sysprof-session-filters-widget.h"
#include "sysprof-session.h"

struct _SysprofSessionFiltersWidget
{
  GtkWidget       parent_instance;

  SysprofSession *session;
};

G_DEFINE_FINAL_TYPE (SysprofSessionFiltersWidget, sysprof_session_filters_widget, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS,
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_session_filters_widget_remove_filter (GtkWidget  *widget,
                                              const char *action_name,
                                              GVariant   *param)
{
  SysprofSessionFiltersWidget *self = (SysprofSessionFiltersWidget *) widget;
  GtkFilter *filter;
  unsigned int position;

  g_assert (SYSPROF_IS_SESSION_FILTERS_WIDGET (widget));
  g_assert (SYSPROF_IS_SESSION (self->session));

  filter = sysprof_session_get_filter (self->session);
  g_assert (GTK_IS_MULTI_FILTER (filter));

  position = g_variant_get_uint32 (param);

  gtk_multi_filter_remove (GTK_MULTI_FILTER (filter), position);
}

static GVariant *
item_to_action_target (gpointer      unused,
                       GtkFilter    *filter,
                       unsigned int  position)
{
  if (filter)
    return g_variant_new_uint32 (position);

  return NULL;
}

static char *
filter_to_icon_name (gpointer   unused,
                     GtkFilter *filter)
{
  if (!filter)
    return g_strdup ("");

  if (SYSPROF_IS_MARK_FILTER (filter))
    return g_strdup ("mark-chart-symbolic");

  return g_strdup ("funnel-outline-symbolic");
}

static char *
filter_to_string (gpointer   unused,
                  GtkFilter *filter)
{
  if (!filter)
    return g_strdup ("");

  if (SYSPROF_IS_MARK_FILTER (filter))
    {
      SysprofMarkFilter *mark_filter = (SysprofMarkFilter *) filter;
      SysprofMarkCatalog *catalog = sysprof_mark_filter_get_catalog (mark_filter);

      return g_strdup_printf ("%s / %s",
                              sysprof_mark_catalog_get_group (catalog),
                              sysprof_mark_catalog_get_name (catalog));
    }

  return g_strdup (G_OBJECT_TYPE_NAME (filter));
}

static void
clear_all_filters (SysprofSessionFiltersWidget *self,
                   GtkButton                   *button)
{
  GtkFilter *filter;

  g_assert (SYSPROF_IS_SESSION_FILTERS_WIDGET (self));
  g_assert (SYSPROF_IS_SESSION (self->session));
  g_assert (GTK_IS_BUTTON (button));

  filter = sysprof_session_get_filter (self->session);
  g_assert (GTK_IS_MULTI_FILTER (filter));

  while (g_list_model_get_n_items (G_LIST_MODEL (filter)) > 0)
    gtk_multi_filter_remove (GTK_MULTI_FILTER (filter), 0);
}

static void
sysprof_session_filters_widget_set_session (SysprofSessionFiltersWidget *self,
                                            SysprofSession              *session)
{
  if (g_set_object (&self->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

static void
sysprof_session_filters_widget_finalize (GObject *object)
{
  SysprofSessionFiltersWidget *self = (SysprofSessionFiltersWidget *)object;

  g_clear_object (&self->session);

  G_OBJECT_CLASS (sysprof_session_filters_widget_parent_class)->finalize (object);
}

static void
sysprof_session_filters_widget_dispose (GObject *object)
{
  SysprofSessionFiltersWidget *self = (SysprofSessionFiltersWidget *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_SESSION_FILTERS_WIDGET);

  G_OBJECT_CLASS (sysprof_session_filters_widget_parent_class)->dispose (object);
}

static void
sysprof_session_filters_widget_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  SysprofSessionFiltersWidget *self = SYSPROF_SESSION_FILTERS_WIDGET (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_filters_widget_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  SysprofSessionFiltersWidget *self = SYSPROF_SESSION_FILTERS_WIDGET (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_session_filters_widget_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_filters_widget_class_init (SysprofSessionFiltersWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_session_filters_widget_finalize;
  object_class->dispose = sysprof_session_filters_widget_dispose;
  object_class->get_property = sysprof_session_filters_widget_get_property;
  object_class->set_property = sysprof_session_filters_widget_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-session-filters-widget.ui");

  gtk_widget_class_set_css_name (widget_class, "sessionfilters");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);

  gtk_widget_class_bind_template_callback (widget_class, clear_all_filters);
  gtk_widget_class_bind_template_callback (widget_class, filter_to_icon_name);
  gtk_widget_class_bind_template_callback (widget_class, filter_to_string);
  gtk_widget_class_bind_template_callback (widget_class, item_to_action_target);

  gtk_widget_class_install_action (widget_class, "sessionfilters.remove-filter", "u", sysprof_session_filters_widget_remove_filter);
}

static void
sysprof_session_filters_widget_init (SysprofSessionFiltersWidget *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
