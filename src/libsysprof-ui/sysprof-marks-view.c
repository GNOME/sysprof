/* sysprof-marks-view.c
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

#define G_LOG_DOMAIN "sysprof-marks-view"

#include "config.h"

#include "sysprof-cell-renderer-duration.h"
#include "sysprof-marks-model.h"
#include "sysprof-marks-view.h"
#include "sysprof-zoom-manager.h"

typedef struct
{
  SysprofZoomManager          *zoom_manager;

  /* Template objects */
  GtkTreeView                 *tree_view;
  SysprofCellRendererDuration *duration_cell;
} SysprofMarksViewPrivate;

enum {
  PROP_0,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (SysprofMarksView, sysprof_marks_view, GTK_TYPE_BIN)

static void
sysprof_marks_view_finalize (GObject *object)
{
  SysprofMarksView *self = (SysprofMarksView *)object;
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);

  g_clear_object (&priv->zoom_manager);

  G_OBJECT_CLASS (sysprof_marks_view_parent_class)->finalize (object);
}

static void
sysprof_marks_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofMarksView *self = SYSPROF_MARKS_VIEW (object);
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, priv->zoom_manager);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofMarksView *self = SYSPROF_MARKS_VIEW (object);
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ZOOM_MANAGER:
      g_set_object (&priv->zoom_manager, g_value_get_object (value));
      g_object_set (priv->duration_cell,
                    "zoom-manager", priv->zoom_manager,
                    NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_view_class_init (SysprofMarksViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_marks_view_finalize;
  object_class->get_property = sysprof_marks_view_get_property;
  object_class->set_property = sysprof_marks_view_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-marks-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, tree_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, duration_cell);

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager", NULL, NULL,
                         SYSPROF_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (SYSPROF_TYPE_CELL_RENDERER_DURATION);
}

static void
sysprof_marks_view_init (SysprofMarksView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_marks_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_MARKS_VIEW, NULL);
}

static void
sysprof_marks_view_load_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(SysprofMarksModel) model = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofMarksViewPrivate *priv;
  SysprofCaptureReader *reader;
  SysprofMarksView *self;
  gint64 capture_begin_time;
  gint64 capture_end_time;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  priv = sysprof_marks_view_get_instance_private (self);

  if (!(model = sysprof_marks_model_new_finish (result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  reader = g_task_get_task_data (task);
  g_assert (reader != NULL);

  capture_begin_time = sysprof_capture_reader_get_start_time (reader);
  capture_end_time = sysprof_capture_reader_get_end_time (reader);

  g_object_set (priv->duration_cell,
                "capture-begin-time", capture_begin_time,
                "capture-end-time", capture_end_time,
                "zoom-manager", priv->zoom_manager,
                NULL);

  gtk_tree_view_set_model (priv->tree_view, GTK_TREE_MODEL (model));

  g_task_return_boolean (task, TRUE);
}

void
sysprof_marks_view_load_async (SysprofMarksView     *self,
                               SysprofCaptureReader *reader,
                               SysprofSelection     *selection,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_MARKS_VIEW (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_marks_view_load_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_ref (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);

  sysprof_marks_model_new_async (reader,
                                 selection,
                                 cancellable,
                                 sysprof_marks_view_load_cb,
                                 g_steal_pointer (&task));
}

gboolean
sysprof_marks_view_load_finish (SysprofMarksView  *self,
                                GAsyncResult      *result,
                                GError           **error)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_VIEW (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
