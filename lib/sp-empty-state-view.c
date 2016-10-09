/* sp-empty-state-view.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#include "sp-empty-state-view.h"

typedef struct
{
  GtkLabel *title;
  GtkLabel *subtitle;
} SpEmptyStateViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SpEmptyStateView, sp_empty_state_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_TITLE,
  PROP_SUBTITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sp_empty_state_view_new (void)
{
  return g_object_new (SP_TYPE_EMPTY_STATE_VIEW, NULL);
}

static void
sp_empty_state_view_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SpEmptyStateView *self = SP_EMPTY_STATE_VIEW (object);
  SpEmptyStateViewPrivate *priv = sp_empty_state_view_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      gtk_label_set_label (priv->title, g_value_get_string (value));
      break;

    case PROP_SUBTITLE:
      gtk_label_set_label (priv->subtitle, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_empty_state_view_class_init (SpEmptyStateViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = sp_empty_state_view_set_property;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         NULL,
                         NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBTITLE] =
    g_param_spec_string ("subtitle",
                         NULL,
                         NULL,
                         NULL,
                         (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sp-empty-state-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SpEmptyStateView, subtitle);
  gtk_widget_class_bind_template_child_private (widget_class, SpEmptyStateView, title);
}

static void
sp_empty_state_view_init (SpEmptyStateView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
