/* sp-cell-renderer-percent.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#include <glib/gi18n.h>

#include "sp-cell-renderer-percent.h"

typedef struct
{
  gdouble percent;
  guint ignore_zero : 1;
} SpCellRendererPercentPrivate;

enum {
  PROP_0,
  PROP_PERCENT,
  PROP_IGNORE_ZERO,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpCellRendererPercent, sp_cell_renderer_percent, GTK_TYPE_CELL_RENDERER_TEXT)

static GParamSpec *properties [N_PROPS];

static void
sp_cell_renderer_percent_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SpCellRendererPercent *self = SP_CELL_RENDERER_PERCENT (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      g_value_set_double (value, sp_cell_renderer_percent_get_percent (self));
      break;

    case PROP_IGNORE_ZERO:
      g_value_set_boolean (value, sp_cell_renderer_percent_get_ignore_zero (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_cell_renderer_percent_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SpCellRendererPercent *self = SP_CELL_RENDERER_PERCENT (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      sp_cell_renderer_percent_set_percent (self, g_value_get_double (value));
      break;

    case PROP_IGNORE_ZERO:
      sp_cell_renderer_percent_set_ignore_zero (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_cell_renderer_percent_class_init (SpCellRendererPercentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sp_cell_renderer_percent_get_property;
  object_class->set_property = sp_cell_renderer_percent_set_property;

  properties [PROP_PERCENT] =
    g_param_spec_double ("percent",
                         "Percent",
                         "Percent",
                         0.0,
                         100.0,
                         0.0,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_IGNORE_ZERO] =
    g_param_spec_boolean ("ignore-zero",
                          "Ignore Zero",
                          "If Zero should be rendered as empty",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_cell_renderer_percent_init (SpCellRendererPercent *self)
{
  g_object_set (self, "xalign", 1.0f, NULL);
}

gdouble
sp_cell_renderer_percent_get_percent (SpCellRendererPercent *self)
{
  SpCellRendererPercentPrivate *priv = sp_cell_renderer_percent_get_instance_private (self);

  g_return_val_if_fail (SP_IS_CELL_RENDERER_PERCENT (self), 0.0);

  return priv->percent;
}

void
sp_cell_renderer_percent_set_percent (SpCellRendererPercent *self,
                                      gdouble                percent)
{
  SpCellRendererPercentPrivate *priv = sp_cell_renderer_percent_get_instance_private (self);

  g_return_if_fail (SP_IS_CELL_RENDERER_PERCENT (self));
  g_return_if_fail (percent >= -100.0);
  g_return_if_fail (percent <= 100.0);

  if (percent != priv->percent)
    {
      gchar text[128];

      priv->percent = percent;

      if (priv->percent != 0.0 || !priv->ignore_zero)
        {
          g_snprintf (text, sizeof text, "%.2lf<span size='smaller'><span size='smaller'> </span>%%</span>", percent);
          text [sizeof text - 1] = '\0';
        }
      else
        {
          text[0] = '\0';
        }

      g_object_set (self, "markup", text, NULL);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PERCENT]);
    }
}

gboolean
sp_cell_renderer_percent_get_ignore_zero (SpCellRendererPercent *self)
{
  SpCellRendererPercentPrivate *priv = sp_cell_renderer_percent_get_instance_private (self);

  g_return_val_if_fail (SP_IS_CELL_RENDERER_PERCENT (self), FALSE);

  return priv->ignore_zero;
}

void
sp_cell_renderer_percent_set_ignore_zero (SpCellRendererPercent *self,
                                          gboolean               ignore_zero)
{
  SpCellRendererPercentPrivate *priv = sp_cell_renderer_percent_get_instance_private (self);

  g_return_if_fail (SP_IS_CELL_RENDERER_PERCENT (self));

  ignore_zero = !!ignore_zero;

  if (ignore_zero != priv->ignore_zero)
    {
      priv->ignore_zero = ignore_zero;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_ZERO]);
    }
}
