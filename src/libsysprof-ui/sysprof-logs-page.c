/* sysprof-logs-page.c
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

#define G_LOG_DOMAIN "sysprof-logs-page"

#include "config.h"

#include "sysprof-log-model.h"
#include "sysprof-logs-page.h"

struct _SysprofLogsPage
{
  SysprofPage parent_instance;

  /* Template Widgets */
  GtkTreeView *tree_view;
};

G_DEFINE_TYPE (SysprofLogsPage, sysprof_logs_page, SYSPROF_TYPE_PAGE)

static void
sysprof_logs_page_load_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  SysprofLogsPage *self;
  g_autoptr(SysprofLogModel) model = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(model = sysprof_log_model_new_finish (result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);

  self = g_task_get_source_object (task);

  gtk_tree_view_set_model (self->tree_view, GTK_TREE_MODEL (model));
}

static void
sysprof_logs_page_load_async (SysprofPage             *page,
                              SysprofCaptureReader    *reader,
                              SysprofSelection        *selection,
                              SysprofCaptureCondition *filter,
                              GCancellable            *cancellable,
                              GAsyncReadyCallback      callback,
                              gpointer                 user_data)
{
  SysprofLogsPage *self = (SysprofLogsPage *)page;
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_LOGS_PAGE (self));
  g_assert (reader != NULL);
  g_assert (!selection || SYSPROF_IS_SELECTION (selection));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_logs_page_load_async);

  sysprof_log_model_new_async (reader,
                               selection,
                               cancellable,
                               sysprof_logs_page_load_cb,
                               g_steal_pointer (&task));
}

static gboolean
sysprof_logs_page_load_finish (SysprofPage   *page,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_assert (SYSPROF_IS_LOGS_PAGE (page));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_logs_page_class_init (SysprofLogsPageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofPageClass *page_class = SYSPROF_PAGE_CLASS (klass);

  page_class->load_async = sysprof_logs_page_load_async;
  page_class->load_finish = sysprof_logs_page_load_finish;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-logs-page.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofLogsPage, tree_view);
}

static void
sysprof_logs_page_init (SysprofLogsPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
