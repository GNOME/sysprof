/* sysprof-chart.c
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

#include "sysprof-chart.h"

typedef struct
{
  SysprofSession *session;
  char *title;
} SysprofChartPrivate;

enum {
  PROP_0,
  PROP_SESSION,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofChart, sysprof_chart, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_chart_size_allocate (GtkWidget *widget,
                             int        width,
                             int        height,
                             int        baseline)
{
  g_assert (SYSPROF_IS_CHART (widget));

  GTK_WIDGET_CLASS (sysprof_chart_parent_class)->size_allocate (widget, width, height, baseline);

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    gtk_widget_size_allocate (child,
                              &(GtkAllocation) {0, 0, width, height},
                              baseline);
}

static void
sysprof_chart_dispose (GObject *object)
{
  SysprofChart *self = (SysprofChart *)object;
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&priv->session);
  g_clear_pointer (&priv->title, g_free);

  G_OBJECT_CLASS (sysprof_chart_parent_class)->dispose (object);
}

static void
sysprof_chart_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  SysprofChart *self = SYSPROF_CHART (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_chart_get_session (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_chart_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  SysprofChart *self = SYSPROF_CHART (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_chart_set_session (self, g_value_get_object (value));
      break;

    case PROP_TITLE:
      sysprof_chart_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_class_init (SysprofChartClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_chart_dispose;
  object_class->get_property = sysprof_chart_get_property;
  object_class->set_property = sysprof_chart_set_property;

  widget_class->size_allocate = sysprof_chart_size_allocate;

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_chart_init (SysprofChart *self)
{
}

const char *
sysprof_chart_get_title (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART (self), NULL);

  return priv->title;
}

/**
 * sysprof_chart_get_session:
 * @self: a #SysprofChart
 *
 * Gets the #SysprofSession of the chart.
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_chart_get_session (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART (self), NULL);

  return priv->session;
}

/**
 * sysprof_chart_set_session:
 * @self: a #SysprofChart
 * @session: (nullable): a #SysprofSession
 *
 * Sets the session for the chart.
 */
void
sysprof_chart_set_session (SysprofChart   *self,
                           SysprofSession *session)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&priv->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

void
sysprof_chart_set_title (SysprofChart *self,
                         const char   *title)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART (self));

  if (g_set_str (&priv->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

void
sysprof_chart_add_layer (SysprofChart      *self,
                         SysprofChartLayer *layer)
{
  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (SYSPROF_IS_CHART_LAYER (layer));
  g_return_if_fail (gtk_widget_get_parent (GTK_WIDGET (layer)) == NULL);

  gtk_widget_set_parent (GTK_WIDGET (layer), GTK_WIDGET (self));
}

void
sysprof_chart_remove_layer (SysprofChart      *self,
                            SysprofChartLayer *layer)
{
  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (SYSPROF_IS_CHART_LAYER (layer));
  g_return_if_fail (gtk_widget_get_parent (GTK_WIDGET (layer)) == GTK_WIDGET (self));

  gtk_widget_unparent (GTK_WIDGET (layer));
}
