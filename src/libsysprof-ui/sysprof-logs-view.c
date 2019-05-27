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

static void
sysprof_logs_view_load_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(SysprofLogModel) model = NULL;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(model = sysprof_log_model_new_finish (result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
    }
  else
    {
      SysprofLogsView *self;

      self = g_task_get_source_object (task);
      gtk_tree_view_set_model (GTK_TREE_VIEW (self->tree_view), GTK_TREE_MODEL (model));
      g_task_return_boolean (task, TRUE);
    }
}

void
sysprof_logs_view_load_async (SysprofLogsView      *self,
                              SysprofCaptureReader *reader,
                              SysprofSelection     *selection,
                              GCancellable         *cancellable,
                              GAsyncReadyCallback   callback,
                              gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_LOGS_VIEW (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_logs_view_load_async);

  sysprof_log_model_new_async (reader,
                               selection,
                               cancellable,
                               sysprof_logs_view_load_cb,
                               g_steal_pointer (&task));
}

gboolean
sysprof_logs_view_load_finish (SysprofLogsView  *self,
                               GAsyncResult     *result,
                               GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_LOGS_VIEW (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
