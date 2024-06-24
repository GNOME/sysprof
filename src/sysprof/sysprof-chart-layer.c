/* sysprof-chart-layer.c
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
#include "sysprof-chart-layer-private.h"

#include "sysprof-session-private.h"
#include "sysprof-window.h"

typedef struct
{
  char *title;
} SysprofChartLayerPrivate;

enum {
  PROP_0,
  PROP_CHART,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SysprofChartLayer, sysprof_chart_layer, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];
static GdkRGBA accent_fg_color;
static GdkRGBA accent_bg_color;

static void
sysprof_chart_layer_css_changed (GtkWidget         *widget,
                                 GtkCssStyleChange *change)
{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  GtkStyleContext *style_context;

  GTK_WIDGET_CLASS (sysprof_chart_layer_parent_class)->css_changed (widget, change);

  style_context = gtk_widget_get_style_context (widget);

  gtk_style_context_lookup_color (style_context, "accent_fg_color", &accent_fg_color);
  gtk_style_context_lookup_color (style_context, "accent_bg_color", &accent_bg_color);
G_GNUC_END_IGNORE_DEPRECATIONS

  gtk_widget_queue_draw (widget);
}

static SysprofSession *
find_session (SysprofChartLayer *self)
{
  GtkWidget *toplevel = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);

  if (SYSPROF_IS_WINDOW (toplevel))
    return sysprof_window_get_session (SYSPROF_WINDOW (toplevel));

  return NULL;
}

static gboolean
sysprof_chart_layer_query_tooltip (GtkWidget  *widget,
                                   int         x,
                                   int         y,
                                   gboolean    keyboard_tooltip,
                                   GtkTooltip *tooltip)
{
  SysprofChartLayer *self = (SysprofChartLayer *)widget;
  g_autoptr(GObject) item = NULL;

  g_assert (SYSPROF_IS_CHART_LAYER (self));
  g_assert (GTK_IS_TOOLTIP (tooltip));

  /* This function is definitely a layering violation, but helpful
   * nonetheless so we don't have to plumb things everywhere. If
   * you were abstracting this into a library, you'd definitely
   * do this differently.
   */

  if ((item = sysprof_chart_layer_lookup_item (self, x, y)))
    {
      SysprofSession *session;

      if ((session = find_session (self)))
        {
          g_autofree char *text = _sysprof_session_describe (session, item);

          if (text != NULL)
            {
              gtk_tooltip_set_text (tooltip, text);
              return TRUE;
            }
        }
    }

  return FALSE;
}

static void
sysprof_chart_layer_dispose (GObject *object)
{
  SysprofChartLayer *self = (SysprofChartLayer *)object;
  SysprofChartLayerPrivate *priv = sysprof_chart_layer_get_instance_private (self);

  g_clear_pointer (&priv->title, g_free);

  G_OBJECT_CLASS (sysprof_chart_layer_parent_class)->dispose (object);
}

static void
sysprof_chart_layer_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofChartLayer *self = SYSPROF_CHART_LAYER (object);

  switch (prop_id)
    {
    case PROP_CHART:
      g_value_set_object (value, gtk_widget_get_ancestor (GTK_WIDGET (self), SYSPROF_TYPE_CHART));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_chart_layer_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofChartLayer *self = SYSPROF_CHART_LAYER (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      sysprof_chart_layer_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_root (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (sysprof_chart_layer_parent_class)->root (widget);

  g_object_notify_by_pspec (G_OBJECT (widget), properties[PROP_CHART]);
}

static void
sysprof_chart_layer_class_init (SysprofChartLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_chart_layer_dispose;
  object_class->get_property = sysprof_chart_layer_get_property;
  object_class->set_property = sysprof_chart_layer_set_property;

  widget_class->css_changed = sysprof_chart_layer_css_changed;
  widget_class->root = sysprof_chart_layer_root;
  widget_class->query_tooltip = sysprof_chart_layer_query_tooltip;

  properties[PROP_CHART] =
    g_param_spec_object ("chart", NULL, NULL,
                         SYSPROF_TYPE_CHART,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "layer");
}

static void
sysprof_chart_layer_init (SysprofChartLayer *self)
{
  gtk_widget_set_has_tooltip (GTK_WIDGET (self), TRUE);
}

const char *
sysprof_chart_layer_get_title (SysprofChartLayer *self)
{
  SysprofChartLayerPrivate *priv = sysprof_chart_layer_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART_LAYER (self), NULL);

  return priv->title;
}

void
sysprof_chart_layer_set_title (SysprofChartLayer *self,
                               const char        *title)
{
  SysprofChartLayerPrivate *priv = sysprof_chart_layer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART_LAYER (self));

  if (g_set_str (&priv->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

void
sysprof_chart_layer_snapshot_motion (SysprofChartLayer *self,
                                     GtkSnapshot       *snapshot,
                                     double             x,
                                     double             y)
{
  g_return_if_fail (SYSPROF_IS_CHART_LAYER (self));
  g_return_if_fail (GTK_IS_SNAPSHOT (snapshot));

  if (SYSPROF_CHART_LAYER_GET_CLASS (self)->snapshot_motion)
    SYSPROF_CHART_LAYER_GET_CLASS (self)->snapshot_motion (self, snapshot, x, y);
}

/**
 * sysprof_chart_layer_lookup_item:
 * @self: a #SysprofChartLayer
 * @x: the coordinate on the X axis
 * @y: the coordinate on the Y axis
 *
 * Locates an item at `(x,y)`.
 *
 * If no item is found at `(x,y)`, then %NULL is returned.
 *
 * Returns: (transfer full) (nullable) (type GObject): A #GObject
 *   if successful; otherwise %NULL.
 */
gpointer
sysprof_chart_layer_lookup_item (SysprofChartLayer *self,
                                 double             x,
                                 double             y)
{
  g_return_val_if_fail (SYSPROF_IS_CHART_LAYER (self), NULL);

  if (SYSPROF_CHART_LAYER_GET_CLASS (self)->lookup_item)
    return SYSPROF_CHART_LAYER_GET_CLASS (self)->lookup_item (self, x, y);

  return NULL;
}

const GdkRGBA *
_sysprof_chart_layer_get_accent_bg_color (void)
{
  return &accent_bg_color;
}

const GdkRGBA *
_sysprof_chart_layer_get_accent_fg_color (void)
{
  return &accent_fg_color;
}
