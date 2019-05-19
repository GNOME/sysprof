/* sysprof-tab.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-tab"

#include "config.h"

#include "sysprof-display.h"
#include "sysprof-tab.h"
#include "sysprof-ui-private.h"

struct _SysprofTab
{
  GtkBox          parent_instance;

  GtkButton      *close_button;
  GtkLabel       *title;
  GtkImage       *recording;

  SysprofDisplay *display;
};

G_DEFINE_TYPE (SysprofTab, sysprof_tab, GTK_TYPE_BOX)

enum {
  PROP_0,
  PROP_DISPLAY,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sysprof_tab_new (SysprofDisplay *display)
{
  return g_object_new (SYSPROF_TYPE_TAB,
                       "display", display,
                       NULL);
}

static void
sysprof_tab_close_clicked (SysprofTab *self,
                           GtkButton  *button)
{
  g_assert (SYSPROF_IS_TAB (self));
  g_assert (GTK_IS_BUTTON (button));

  if (self->display != NULL)
    gtk_widget_destroy (GTK_WIDGET (self->display));
}

static void
sysprof_tab_finalize (GObject *object)
{
  SysprofTab *self = (SysprofTab *)object;

  g_clear_weak_pointer (&self->display);

  G_OBJECT_CLASS (sysprof_tab_parent_class)->finalize (object);
}

static void
sysprof_tab_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SysprofTab *self = SYSPROF_TAB (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, self->display);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tab_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SysprofTab *self = SYSPROF_TAB (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_set_weak_pointer (&self->display, g_value_get_object (value));
      g_object_bind_property (self->display, "title", self->title, "label", G_BINDING_SYNC_CREATE);
      g_object_bind_property (self->display, "recording", self->recording, "visible", G_BINDING_SYNC_CREATE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tab_class_init (SysprofTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_tab_finalize;
  object_class->get_property = sysprof_tab_get_property;
  object_class->set_property = sysprof_tab_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-tab.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofTab, close_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofTab, recording);
  gtk_widget_class_bind_template_child (widget_class, SysprofTab, title);

  properties [PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "The display widget for the tab",
                         SYSPROF_TYPE_DISPLAY,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_tab_init (SysprofTab *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->close_button,
                           "clicked",
                           G_CALLBACK (sysprof_tab_close_clicked),
                           self,
                           G_CONNECT_SWAPPED);
}
