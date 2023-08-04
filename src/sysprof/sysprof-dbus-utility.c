/* sysprof-dbus-utility.c
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

#include "sysprof-dbus-utility.h"

struct _SysprofDBusUtility
{
  GtkWidget                   parent_instance;
  SysprofDocumentDBusMessage *message;
};

enum {
  PROP_0,
  PROP_MESSAGE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDBusUtility, sysprof_dbus_utility, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_dbus_utility_dispose (GObject *object)
{
  SysprofDBusUtility *self = (SysprofDBusUtility *)object;
  GtkWidget *child;

  g_clear_object (&self->message);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_dbus_utility_parent_class)->dispose (object);
}

static void
sysprof_dbus_utility_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofDBusUtility *self = SYSPROF_DBUS_UTILITY (object);

  switch (prop_id)
    {
    case PROP_MESSAGE:
      g_value_set_object (value, sysprof_dbus_utility_get_message (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_dbus_utility_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofDBusUtility *self = SYSPROF_DBUS_UTILITY (object);

  switch (prop_id)
    {
    case PROP_MESSAGE:
      sysprof_dbus_utility_set_message (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_dbus_utility_class_init (SysprofDBusUtilityClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_dbus_utility_dispose;
  object_class->get_property = sysprof_dbus_utility_get_property;
  object_class->set_property = sysprof_dbus_utility_set_property;

  properties[PROP_MESSAGE] =
    g_param_spec_object ("message", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-dbus-utility.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_dbus_utility_init (SysprofDBusUtility *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

SysprofDocumentDBusMessage *
sysprof_dbus_utility_get_message (SysprofDBusUtility *self)
{
  g_return_val_if_fail (SYSPROF_IS_DBUS_UTILITY (self), NULL);

  return self->message;
}

void
sysprof_dbus_utility_set_message (SysprofDBusUtility         *self,
                                  SysprofDocumentDBusMessage *message)
{
  g_return_if_fail (SYSPROF_IS_DBUS_UTILITY (self));
  g_return_if_fail (!message || SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (message));

  if (g_set_object (&self->message, message))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESSAGE]);
}
