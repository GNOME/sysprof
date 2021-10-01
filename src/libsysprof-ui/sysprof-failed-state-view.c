/* sysprof-failed-state-view.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-failed-state-view.h"

G_DEFINE_TYPE (SysprofFailedStateView, sysprof_failed_state_view, GTK_TYPE_WIDGET)

GtkWidget *
sysprof_failed_state_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_FAILED_STATE_VIEW, NULL);
}

static void
sysprof_failed_state_view_dispose (GObject *object)
{
  SysprofFailedStateView *self = (SysprofFailedStateView *)object;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_failed_state_view_parent_class)->dispose (object);
}

static void
sysprof_failed_state_view_class_init (SysprofFailedStateViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_failed_state_view_dispose;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sysprof-failed-state-view.ui");
}

static void
sysprof_failed_state_view_init (SysprofFailedStateView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
