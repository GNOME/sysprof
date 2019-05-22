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
  SysprofMarksModelKind        kind;

  SysprofZoomManager          *zoom_manager;

  gint64                       capture_begin_time;
  gint64                       capture_end_time;

  /* Template objects */
  GtkScrolledWindow           *scroller;
  GtkTreeView                 *tree_view;
  GtkTreeViewColumn           *duration_column;
  SysprofCellRendererDuration *duration_cell;
  GtkStack                    *stack;
} SysprofMarksViewPrivate;

enum {
  PROP_0,
  PROP_KIND,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (SysprofMarksView, sysprof_marks_view, GTK_TYPE_BIN)

static gboolean
sysprof_marks_view_tree_view_key_press_event_cb (SysprofMarksView  *self,
                                                 const GdkEventKey *key,
                                                 GtkTreeView       *tree_view)
{
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);
  gint dir = 0;

  g_assert (SYSPROF_MARKS_VIEW (self));
  g_assert (key != NULL);

  if (key->state == 0)
    {
      switch (key->keyval)
        {
        case GDK_KEY_Left:
          dir = -1;
          break;

        case GDK_KEY_Right:
          dir = 1;
          break;

        default:
          break;
        }

      if (dir)
        {
          GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment (priv->scroller);
          gdouble step = gtk_adjustment_get_step_increment (adj);
          gdouble val = CLAMP (gtk_adjustment_get_value (adj) + (step * dir),
                               gtk_adjustment_get_lower (adj),
                               gtk_adjustment_get_upper (adj));
          gtk_adjustment_set_value (adj, val);
          return GDK_EVENT_STOP;
        }
    }

  return GDK_EVENT_PROPAGATE;
}

static gboolean
get_first_selected (GtkTreeSelection  *selection,
                    GtkTreeModel     **model,
                    GtkTreeIter       *iter)
{
  GtkTreeModel *m;

  g_assert (GTK_IS_TREE_SELECTION (selection));

  if (gtk_tree_selection_count_selected_rows (selection) != 1)
    return FALSE;

  m = gtk_tree_view_get_model (gtk_tree_selection_get_tree_view (selection));
  if (model)
    *model = m;

  if (iter)
    {
      GList *paths = gtk_tree_selection_get_selected_rows (selection, model);
      gtk_tree_model_get_iter (m, iter, paths->data);
      g_list_free_full (paths, (GDestroyNotify)gtk_tree_path_free);
    }

  return TRUE;
}

static void
sysprof_marks_view_selection_changed_cb (SysprofMarksView *self,
                                         GtkTreeSelection *selection)
{
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_MARKS_VIEW (self));
  g_assert (GTK_IS_TREE_SELECTION (selection));

  if (get_first_selected (selection, &model, &iter))
    {
      GtkAdjustment *adj;
      gdouble x;
      gint64 begin_time;
      gdouble lower;
      gdouble upper;
      gdouble value;
      gdouble page_size;
      gint width;

      gtk_tree_model_get (model, &iter,
                          SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME, &begin_time,
                          -1);

      adj = gtk_scrolled_window_get_hadjustment (priv->scroller);
      width = gtk_tree_view_column_get_width (priv->duration_column);
      x = sysprof_zoom_manager_get_offset_at_time (priv->zoom_manager,
                                                   begin_time - priv->capture_begin_time,
                                                   width);

      g_object_get (adj,
                    "lower", &lower,
                    "upper", &upper,
                    "value", &value,
                    "page-size", &page_size,
                    NULL);

      if (x < value)
        gtk_adjustment_set_value (adj, MAX (lower, x - (page_size / 3.0)));
      else if (x > (value + page_size))
        gtk_adjustment_set_value (adj, MIN (upper - page_size, (x - (page_size / 3.0))));
    }
}

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
    case PROP_KIND:
      g_value_set_enum (value, priv->kind);
      break;

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
    case PROP_KIND:
      priv->kind = g_value_get_enum (value);
      break;

    case PROP_ZOOM_MANAGER:
      if (g_set_object (&priv->zoom_manager, g_value_get_object (value)))
        {
          g_object_set (priv->duration_cell,
                        "zoom-manager", priv->zoom_manager,
                        NULL);
          if (priv->zoom_manager)
            g_signal_connect_object (priv->zoom_manager,
                                     "notify::zoom",
                                     G_CALLBACK (gtk_tree_view_column_queue_resize),
                                     priv->duration_column,
                                     G_CONNECT_SWAPPED);
        }
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
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, scroller);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, tree_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, duration_cell);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, duration_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksView, stack);

  properties [PROP_KIND] =
    g_param_spec_enum ("kind", NULL, NULL,
                       SYSPROF_TYPE_MARKS_MODEL_KIND,
                       SYSPROF_MARKS_MODEL_MARKS,
                       (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);

  priv->kind = SYSPROF_MARKS_MODEL_MARKS;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (priv->tree_view),
                               GTK_SELECTION_MULTIPLE);

  g_signal_connect_object (priv->tree_view,
                           "key-press-event",
                           G_CALLBACK (sysprof_marks_view_tree_view_key_press_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (gtk_tree_view_get_selection (priv->tree_view),
                           "changed",
                           G_CALLBACK (sysprof_marks_view_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
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

  priv->capture_begin_time = sysprof_capture_reader_get_start_time (reader);
  priv->capture_end_time = sysprof_capture_reader_get_end_time (reader);

  g_object_set (priv->duration_cell,
                "capture-begin-time", priv->capture_begin_time,
                "capture-end-time", priv->capture_end_time,
                "zoom-manager", priv->zoom_manager,
                NULL);

  gtk_tree_view_set_model (priv->tree_view, GTK_TREE_MODEL (model));

  if (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) == 0)
    gtk_stack_set_visible_child_name (priv->stack, "empty-state");
  else
    gtk_stack_set_visible_child_name (priv->stack, "marks");

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
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);
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
                                 priv->kind,
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

void
_sysprof_marks_view_set_hadjustment (SysprofMarksView *self,
                                     GtkAdjustment    *hadjustment)
{
  SysprofMarksViewPrivate *priv = sysprof_marks_view_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_MARKS_VIEW (self));
  g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));

  gtk_scrolled_window_set_hadjustment (priv->scroller, hadjustment);
}
