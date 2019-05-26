/* sysprof-logs-view.c
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

#define G_LOG_DOMAIN "sysprof-logs-view"

#include "config.h"

#include "sysprof-log-model.h"
#include "sysprof-logs-view.h"

struct _SysprofLogsView
{
  GtkBin parent_instance;

  GtkTreeView *tree_view;
};

G_DEFINE_TYPE (SysprofLogsView, sysprof_logs_view, GTK_TYPE_BIN)

static void
sysprof_logs_view_class_init (SysprofLogsViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-logs-view.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofLogsView, tree_view);
}

static void
sysprof_logs_view_init (SysprofLogsView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
sysprof_logs_view_set_model (SysprofLogsView *self,
                             SysprofLogModel *model)
{
  g_return_if_fail (SYSPROF_IS_LOGS_VIEW (self));
  g_return_if_fail (!model || SYSPROF_IS_LOG_MODEL (model));

  gtk_tree_view_set_model (GTK_TREE_VIEW (self->tree_view), GTK_TREE_MODEL (model));
} 
