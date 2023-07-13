/* sysprof-time-scrubber.c
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

#include <sysprof-gtk.h>

#include "sysprof-time-scrubber.h"

struct _SysprofTimeScrubber
{
  GtkWidget         parent_instance;

  SysprofSession   *session;

  SysprofTimeRuler *ruler;
  GtkBox           *vbox;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

static void buildable_iface_init (GtkBuildableIface *iface);
static GtkBuildableIface *buildable_parent;

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofTimeScrubber, sysprof_time_scrubber, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties[N_PROPS];

static void
sysprof_time_scrubber_dispose (GObject *object)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TIME_SCRUBBER);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->session);

  G_OBJECT_CLASS (sysprof_time_scrubber_parent_class)->dispose (object);
}

static void
sysprof_time_scrubber_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofTimeScrubber *self = SYSPROF_TIME_SCRUBBER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_time_scrubber_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_scrubber_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofTimeScrubber *self = SYSPROF_TIME_SCRUBBER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_time_scrubber_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_scrubber_class_init (SysprofTimeScrubberClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_time_scrubber_dispose;
  object_class->get_property = sysprof_time_scrubber_get_property;
  object_class->set_property = sysprof_time_scrubber_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-time-scrubber.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, vbox);
  gtk_widget_class_bind_template_child (widget_class, SysprofTimeScrubber, ruler);

  g_type_ensure (SYSPROF_TYPE_TIME_RULER);
}

static void
sysprof_time_scrubber_init (SysprofTimeScrubber *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

SysprofSession *
sysprof_time_scrubber_get_session (SysprofTimeScrubber *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_SCRUBBER (self), NULL);

  return self->session;
}

void
sysprof_time_scrubber_set_session (SysprofTimeScrubber *self,
                                   SysprofSession      *session)
{
  g_return_if_fail (SYSPROF_IS_TIME_SCRUBBER (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&self->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

static void
sysprof_time_scrubber_add_chart (SysprofTimeScrubber *self,
                                 GtkWidget           *chart)
{
  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));
  g_assert (GTK_IS_WIDGET (chart));

  gtk_box_append (self->vbox, chart);
}

static void
sysprof_time_scrubber_add_child (GtkBuildable *buildable,
                                 GtkBuilder   *builder,
                                 GObject      *object,
                                 const char   *type)
{
  SysprofTimeScrubber *self = (SysprofTimeScrubber *)buildable;

  g_assert (SYSPROF_IS_TIME_SCRUBBER (self));

  if (g_strcmp0 (type, "chart") == 0)
    sysprof_time_scrubber_add_chart (self, GTK_WIDGET (object));
  else
    buildable_parent->add_child (buildable, builder, object, type);
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  buildable_parent = g_type_interface_peek_parent (iface);
  iface->add_child = sysprof_time_scrubber_add_child;
}
