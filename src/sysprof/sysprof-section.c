/* sysprof-section.c
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

#include "sysprof-section.h"
#include "sysprof-window.h"

typedef struct
{
  char *category;
  char *title;
  SysprofSession *session;
} SysprofSectionPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SysprofSection, sysprof_section, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_SESSION,
  PROP_CATEGORY,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_section_dispose (GObject *object)
{
  SysprofSection *self = (SysprofSection *)object;
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&priv->session);
  g_clear_pointer (&priv->title, g_free);
  g_clear_pointer (&priv->category, g_free);

  G_OBJECT_CLASS (sysprof_section_parent_class)->dispose (object);
}

static void
sysprof_section_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofSection *self = SYSPROF_SECTION (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_section_get_session (self));
      break;

    case PROP_CATEGORY:
      g_value_set_string (value, sysprof_section_get_category (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_section_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_section_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofSection *self = SYSPROF_SECTION (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_section_set_session (self, g_value_get_object (value));
      break;

    case PROP_CATEGORY:
      sysprof_section_set_category (self, g_value_get_string (value));
      break;

    case PROP_TITLE:
      sysprof_section_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_section_class_init (SysprofSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_section_dispose;
  object_class->get_property = sysprof_section_get_property;
  object_class->set_property = sysprof_section_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_CATEGORY] =
    g_param_spec_string ("category", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_section_init (SysprofSection *self)
{
}

SysprofSession *
sysprof_section_get_session (SysprofSection *self)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_SECTION (self), NULL);

  return priv->session;
}

void
sysprof_section_set_session (SysprofSection *self,
                             SysprofSession *session)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_SECTION (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&priv->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

const char *
sysprof_section_get_title (SysprofSection *self)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_SECTION (self), NULL);

  return priv->title;
}

void
sysprof_section_set_title (SysprofSection *self,
                           const char *title)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_SECTION (self));

  if (g_set_str (&priv->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

const char *
sysprof_section_get_category (SysprofSection *self)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_SECTION (self), NULL);

  return priv->category;
}

void
sysprof_section_set_category (SysprofSection *self,
                              const char     *category)
{
  SysprofSectionPrivate *priv = sysprof_section_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_SECTION (self));

  if (g_set_str (&priv->category, category))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY]);
}
