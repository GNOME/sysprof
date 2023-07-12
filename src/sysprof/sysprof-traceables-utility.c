/* sysprof-traceables-utility.c
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

#include "sysprof-traceables-utility.h"

struct _SysprofTraceablesUtility
{
  GtkWidget       parent_instance;
  SysprofSession *session;
  GListModel     *traceables;
};

enum {
  PROP_0,
  PROP_SESSION,
  PROP_TRACEABLES,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTraceablesUtility, sysprof_traceables_utility, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static void
sysprof_traceables_utility_finalize (GObject *object)
{
  SysprofTraceablesUtility *self = (SysprofTraceablesUtility *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TRACEABLES_UTILITY);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (object))))
    gtk_widget_unparent (child);

  g_clear_object (&self->session);

  G_OBJECT_CLASS (sysprof_traceables_utility_parent_class)->finalize (object);
}

static void
sysprof_traceables_utility_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofTraceablesUtility *self = SYSPROF_TRACEABLES_UTILITY (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;

    case PROP_TRACEABLES:
      g_value_set_object (value, self->traceables);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_traceables_utility_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofTraceablesUtility *self = SYSPROF_TRACEABLES_UTILITY (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_set_object (&self->session, g_value_get_object (value));
      break;

    case PROP_TRACEABLES:
      g_set_object (&self->traceables, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_traceables_utility_class_init (SysprofTraceablesUtilityClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_traceables_utility_finalize;
  object_class->get_property = sysprof_traceables_utility_get_property;
  object_class->set_property = sysprof_traceables_utility_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TRACEABLES] =
    g_param_spec_object ("traceables", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-traceables-utility.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_traceables_utility_init (SysprofTraceablesUtility *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
