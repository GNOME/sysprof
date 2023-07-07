/* sysprof-greeter.c
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

#include "sysprof-greeter.h"

struct _SysprofGreeter
{
  AdwWindow  parent_instance;

  GtkBox             *toolbar;
  AdwPreferencesPage *record_page;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofGreeter, sysprof_greeter, ADW_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
sysprof_greeter_view_stack_notify_visible_child (SysprofGreeter *self,
                                                 GParamSpec     *pspec,
                                                 AdwViewStack   *stack)
{
  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (ADW_IS_VIEW_STACK (stack));

  gtk_widget_set_visible (GTK_WIDGET (self->toolbar),
                          GTK_WIDGET (self->record_page) == adw_view_stack_get_visible_child (stack));
}

static void
sysprof_greeter_dispose (GObject *object)
{
  SysprofGreeter *self = (SysprofGreeter *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_GREETER);

  G_OBJECT_CLASS (sysprof_greeter_parent_class)->dispose (object);
}

static void
sysprof_greeter_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofGreeter *self = SYSPROF_GREETER (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_greeter_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofGreeter *self = SYSPROF_GREETER (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_greeter_class_init (SysprofGreeterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_greeter_dispose;
  object_class->get_property = sysprof_greeter_get_property;
  object_class->set_property = sysprof_greeter_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-greeter.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, toolbar);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_page);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_greeter_view_stack_notify_visible_child);
}

static void
sysprof_greeter_init (SysprofGreeter *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_greeter_new (void)
{
  return g_object_new (SYSPROF_TYPE_GREETER, NULL);
}
