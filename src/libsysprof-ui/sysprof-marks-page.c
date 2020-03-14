/* sysprof-marks-page.c
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

#define G_LOG_DOMAIN "sysprof-marks-page"

#include "config.h"

#include "sysprof-cell-renderer-duration.h"
#include "sysprof-marks-model.h"
#include "sysprof-marks-page.h"
#include "sysprof-ui-private.h"
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
  GtkBox                      *details_box;
  GtkTreeViewColumn           *duration_column;
  SysprofCellRendererDuration *duration_cell;
  GtkStack                    *stack;
  GtkLabel                    *group;
  GtkLabel                    *mark;
  GtkLabel                    *time;
  GtkLabel                    *end;
  GtkLabel                    *duration;
  GtkTextView                 *message;
} SysprofMarksPagePrivate;

enum {
  PROP_0,
  PROP_KIND,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (SysprofMarksPage, sysprof_marks_page, SYSPROF_TYPE_PAGE)

static gboolean
sysprof_marks_page_tree_view_key_press_event_cb (SysprofMarksPage  *self,
                                                 const GdkEventKey *key,
                                                 GtkTreeView       *tree_view)
{
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);
  gint dir = 0;

  g_assert (SYSPROF_MARKS_PAGE (self));
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
sysprof_marks_page_selection_changed_cb (SysprofMarksPage *self,
                                         GtkTreeSelection *selection)
{
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_MARKS_PAGE (self));
  g_assert (GTK_IS_TREE_SELECTION (selection));

  if (get_first_selected (selection, &model, &iter))
    {
      g_autofree gchar *group = NULL;
      g_autofree gchar *name = NULL;
      g_autofree gchar *duration_str = NULL;
      g_autofree gchar *time_str = NULL;
      g_autofree gchar *end_str = NULL;
      g_autofree gchar *text = NULL;
      GtkAdjustment *adj;
      gdouble x;
      gint64 begin_time;
      gint64 end_time;
      gint64 duration;
      gint64 etime;
      gint64 otime;
      gdouble lower;
      gdouble upper;
      gdouble value;
      gdouble page_size;
      gint width;

      gtk_tree_model_get (model, &iter,
                          SYSPROF_MARKS_MODEL_COLUMN_GROUP, &group,
                          SYSPROF_MARKS_MODEL_COLUMN_NAME, &name,
                          SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME, &begin_time,
                          SYSPROF_MARKS_MODEL_COLUMN_END_TIME, &end_time,
                          SYSPROF_MARKS_MODEL_COLUMN_TEXT, &text,
                          -1);

      duration = end_time - begin_time;
      duration_str = _sysprof_format_duration (duration);

      otime = begin_time - priv->capture_begin_time;
      time_str = _sysprof_format_duration (otime);

      etime = end_time - priv->capture_begin_time;
      end_str = _sysprof_format_duration (etime);

      gtk_label_set_label (priv->group, group);
      gtk_label_set_label (priv->mark, name);
      gtk_label_set_label (priv->duration, duration_str);
      gtk_label_set_label (priv->time, time_str);
      gtk_label_set_label (priv->end, end_str);

      gtk_text_buffer_set_text (gtk_text_view_get_buffer (priv->message), text, -1);

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

static gboolean
sysprof_marks_page_tree_view_query_tooltip_cb (SysprofMarksPage *self,
                                               gint              x,
                                               gint              y,
                                               gboolean          keyboard_mode,
                                               GtkTooltip       *tooltip,
                                               GtkTreeView      *tree_view)
{
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);
  GtkTreeViewColumn *column;
  GtkTreePath *path = NULL;
  gint cell_x, cell_y;
  gboolean ret = FALSE;

  g_assert (SYSPROF_IS_MARKS_PAGE (self));
  g_assert (GTK_IS_TOOLTIP (tooltip));
  g_assert (GTK_IS_TREE_VIEW (tree_view));

  if (gtk_tree_view_get_path_at_pos (tree_view, x, y, &path, &column, &cell_x, &cell_y))
    {
      GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
      GtkTreeIter iter;

      if (gtk_tree_model_get_iter (model, &iter, path))
        {
          g_autofree gchar *text = NULL;
          g_autofree gchar *timestr = NULL;
          g_autofree gchar *tooltip_text = NULL;
          g_autofree gchar *durationstr = NULL;
          gint64 begin_time;
          gint64 end_time;
          gint64 duration;

          gtk_tree_model_get (model, &iter,
                              SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME, &begin_time,
                              SYSPROF_MARKS_MODEL_COLUMN_END_TIME, &end_time,
                              SYSPROF_MARKS_MODEL_COLUMN_TEXT, &text,
                              -1);

          duration = end_time - begin_time;
          begin_time -= priv->capture_begin_time;
          durationstr = _sysprof_format_duration (duration);

          if (duration != 0)
            timestr = g_strdup_printf ("%0.4lf (%s)", begin_time / (gdouble)SYSPROF_NSEC_PER_SEC, durationstr);
          else
            timestr = g_strdup_printf ("%0.4lf", begin_time / (gdouble)SYSPROF_NSEC_PER_SEC);

          tooltip_text = g_strdup_printf ("%s: %s", timestr, text);

          gtk_tooltip_set_text (tooltip, tooltip_text);

          ret = TRUE;
        }
    }

  gtk_tree_path_free (path);

  return ret;
}

static void
sysprof_marks_page_load_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(SysprofMarksModel) model = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofMarksPagePrivate *priv;
  SysprofCaptureReader *reader;
  SysprofMarksPage *self;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  priv = sysprof_marks_page_get_instance_private (self);

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

static void
sysprof_marks_page_load_async (SysprofPage             *page,
                               SysprofCaptureReader    *reader,
                               SysprofSelection        *selection,
                               SysprofCaptureCondition *filter,
                               GCancellable            *cancellable,
                               GAsyncReadyCallback      callback,
                               gpointer                 user_data)
{
  SysprofMarksPage *self = (SysprofMarksPage *)page;
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_MARKS_PAGE (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_marks_page_load_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_ref (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);

  sysprof_marks_model_new_async (reader,
                                 priv->kind,
                                 selection,
                                 cancellable,
                                 sysprof_marks_page_load_cb,
                                 g_steal_pointer (&task));
}

static gboolean
sysprof_marks_page_load_finish (SysprofPage   *page,
                                GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_PAGE (page), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_marks_page_set_hadjustment (SysprofPage   *page,
                                    GtkAdjustment *hadjustment)
{
  SysprofMarksPage *self = (SysprofMarksPage *)page;
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

  g_assert (SYSPROF_IS_MARKS_PAGE (self));
  g_assert (!hadjustment || GTK_IS_ADJUSTMENT (hadjustment));

  gtk_scrolled_window_set_hadjustment (priv->scroller, hadjustment);
}

static void
sysprof_marks_page_set_size_group (SysprofPage  *page,
                                   GtkSizeGroup *size_group)
{
  SysprofMarksPage *self = (SysprofMarksPage *)page;
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

  g_assert (SYSPROF_IS_MARKS_PAGE (self));
  g_assert (GTK_IS_SIZE_GROUP (size_group));

  gtk_size_group_add_widget (size_group, GTK_WIDGET (priv->details_box));
}

static void
sysprof_marks_page_tree_view_row_activated_cb (SysprofMarksPage  *self,
                                               GtkTreePath       *path,
                                               GtkTreeViewColumn *column,
                                               GtkTreeView       *tree_view)
{
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_MARKS_PAGE (self));
  g_assert (path != NULL);
  g_assert (GTK_IS_TREE_VIEW_COLUMN (column));
  g_assert (GTK_IS_TREE_VIEW (tree_view));

  model = gtk_tree_view_get_model (tree_view);

  if (gtk_tree_model_get_iter (model, &iter, path))
    {
      SysprofDisplay *display;
      gint64 begin_time;
      gint64 end_time;

      gtk_tree_model_get (model, &iter,
                          SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME, &begin_time,
                          SYSPROF_MARKS_MODEL_COLUMN_END_TIME, &end_time,
                          -1);

      display = SYSPROF_DISPLAY (gtk_widget_get_ancestor (GTK_WIDGET (self), SYSPROF_TYPE_DISPLAY));
      sysprof_display_add_to_selection (display, begin_time, end_time);
    }
}

static void
sysprof_marks_page_finalize (GObject *object)
{
  SysprofMarksPage *self = (SysprofMarksPage *)object;
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

  g_clear_object (&priv->zoom_manager);

  G_OBJECT_CLASS (sysprof_marks_page_parent_class)->finalize (object);
}

static void
sysprof_marks_page_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofMarksPage *self = SYSPROF_MARKS_PAGE (object);
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

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
sysprof_marks_page_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofMarksPage *self = SYSPROF_MARKS_PAGE (object);
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

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
sysprof_marks_page_class_init (SysprofMarksPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofPageClass *page_class = SYSPROF_PAGE_CLASS (klass);

  object_class->finalize = sysprof_marks_page_finalize;
  object_class->get_property = sysprof_marks_page_get_property;
  object_class->set_property = sysprof_marks_page_set_property;

  page_class->load_async = sysprof_marks_page_load_async;
  page_class->load_finish = sysprof_marks_page_load_finish;
  page_class->set_hadjustment = sysprof_marks_page_set_hadjustment;
  page_class->set_size_group = sysprof_marks_page_set_size_group;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-marks-page.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, end);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, details_box);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, duration_cell);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, duration_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, scroller);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, stack);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, tree_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, group);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, mark);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, duration);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, time);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofMarksPage, message);

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
sysprof_marks_page_init (SysprofMarksPage *self)
{
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

  priv->kind = SYSPROF_MARKS_MODEL_MARKS;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (priv->tree_view),
                               GTK_SELECTION_MULTIPLE);

  g_signal_connect_object (priv->tree_view,
                           "key-press-event",
                           G_CALLBACK (sysprof_marks_page_tree_view_key_press_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->tree_view,
                           "row-activated",
                           G_CALLBACK (sysprof_marks_page_tree_view_row_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->tree_view,
                           "query-tooltip",
                           G_CALLBACK (sysprof_marks_page_tree_view_query_tooltip_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (gtk_tree_view_get_selection (priv->tree_view),
                           "changed",
                           G_CALLBACK (sysprof_marks_page_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
sysprof_marks_page_new (SysprofZoomManager    *zoom_manager,
                        SysprofMarksModelKind  kind)
{
  SysprofMarksPage *self;
  SysprofMarksPagePrivate *priv;

  g_return_val_if_fail (SYSPROF_IS_ZOOM_MANAGER (zoom_manager), NULL);

  self = g_object_new (SYSPROF_TYPE_MARKS_PAGE,
                       "zoom-manager", zoom_manager,
                       NULL);
  priv = sysprof_marks_page_get_instance_private (self);
  priv->kind = kind;

  return GTK_WIDGET (self);
}

void
_sysprof_marks_page_set_hadjustment (SysprofMarksPage *self,
                                     GtkAdjustment    *hadjustment)
{
  SysprofMarksPagePrivate *priv = sysprof_marks_page_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_MARKS_PAGE (self));
  g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));

  gtk_scrolled_window_set_hadjustment (priv->scroller, hadjustment);
}
