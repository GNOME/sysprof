/* sysprof-cell-renderer-percent.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "sysprof-cell-renderer-percent"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-cell-renderer-percent.h"

typedef struct
{
  gdouble percent;
} SysprofCellRendererPercentPrivate;

enum {
  PROP_0,
  PROP_PERCENT,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofCellRendererPercent, sysprof_cell_renderer_percent, GTK_TYPE_CELL_RENDERER_PROGRESS)

static GParamSpec *properties [N_PROPS];

static void
sysprof_cell_renderer_percent_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  SysprofCellRendererPercent *self = SYSPROF_CELL_RENDERER_PERCENT (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      g_value_set_double (value, sysprof_cell_renderer_percent_get_percent (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cell_renderer_percent_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  SysprofCellRendererPercent *self = SYSPROF_CELL_RENDERER_PERCENT (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      sysprof_cell_renderer_percent_set_percent (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cell_renderer_percent_class_init (SysprofCellRendererPercentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_cell_renderer_percent_get_property;
  object_class->set_property = sysprof_cell_renderer_percent_set_property;

  properties [PROP_PERCENT] =
    g_param_spec_double ("percent",
                         "Percent",
                         "Percent",
                         0.0,
                         100.0,
                         0.0,
                         /* Doesn't notify to avoid signal emission */
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_cell_renderer_percent_init (SysprofCellRendererPercent *self)
{
  g_object_set (self, "text-xalign", 1.0f, NULL);
}

gdouble
sysprof_cell_renderer_percent_get_percent (SysprofCellRendererPercent *self)
{
  SysprofCellRendererPercentPrivate *priv = sysprof_cell_renderer_percent_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CELL_RENDERER_PERCENT (self), 0.0);

  return priv->percent;
}

void
sysprof_cell_renderer_percent_set_percent (SysprofCellRendererPercent *self,
                                           gdouble                     percent)
{
  SysprofCellRendererPercentPrivate *priv = sysprof_cell_renderer_percent_get_instance_private (self);
  gchar text[8];

  g_return_if_fail (SYSPROF_IS_CELL_RENDERER_PERCENT (self));
  g_return_if_fail (percent >= 0.0);
  g_return_if_fail (percent <= 100.0);

  priv->percent = percent;

  g_snprintf (text, sizeof text, "%.2lf%%", percent);
  text [sizeof text - 1] = '\0';

  g_object_set (self,
                "value", (guint)percent,
                "text", text,
                NULL);
}
