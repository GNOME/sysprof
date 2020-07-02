/* sysprof-log-model.c
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

#define G_LOG_DOMAIN "sysprof-log-model"

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>

#include "sysprof-log-model.h"

struct _SysprofLogModel
{
  GObject       parent_instance;
  GStringChunk *chunks;
  GArray       *items;
  gint64        begin_time;
};

typedef struct
{
  gint64         time;
  const gchar   *domain;
  const gchar   *message;
  guint16        severity;
} Item;

static gint
sysprof_log_model_get_n_columns (GtkTreeModel *model)
{
  return SYSPROF_LOG_MODEL_COLUMN_LAST;
}

static GType
sysprof_log_model_get_column_type (GtkTreeModel *model,
                                   gint          column)
{
  switch (column)
    {
    case SYSPROF_LOG_MODEL_COLUMN_TIME:
      return G_TYPE_INT64;

    case SYSPROF_LOG_MODEL_COLUMN_SEVERITY:
    case SYSPROF_LOG_MODEL_COLUMN_DOMAIN:
    case SYSPROF_LOG_MODEL_COLUMN_MESSAGE:
    case SYSPROF_LOG_MODEL_COLUMN_TIME_STRING:
      return G_TYPE_STRING;

    default:
      return 0;
    }
}

static GtkTreePath *
sysprof_log_model_get_path (GtkTreeModel *model,
                            GtkTreeIter  *iter)
{
  gint off;

  g_assert (SYSPROF_IS_LOG_MODEL (model));
  g_assert (iter != NULL);

  off = GPOINTER_TO_INT (iter->user_data);

  return gtk_tree_path_new_from_indices (off, -1);
}

static gboolean
sysprof_log_model_get_iter (GtkTreeModel *model,
                            GtkTreeIter  *iter,
                            GtkTreePath  *path)
{
  SysprofLogModel *self = (SysprofLogModel *)model;
  gint off;

  g_assert (SYSPROF_IS_LOG_MODEL (self));
  g_assert (iter != NULL);
  g_assert (path != NULL);

  memset (iter, 0, sizeof *iter);

  if (gtk_tree_path_get_depth (path) != 1)
    return FALSE;

  off = gtk_tree_path_get_indices (path)[0];
  iter->user_data = GINT_TO_POINTER (off);

  return off >= 0 && off < self->items->len;
}

static gboolean
sysprof_log_model_iter_next (GtkTreeModel *model,
                             GtkTreeIter  *iter)
{
  SysprofLogModel *self = (SysprofLogModel *)model;
  gint off;

  g_assert (SYSPROF_IS_LOG_MODEL (self));
  g_assert (iter != NULL);

  off = GPOINTER_TO_INT (iter->user_data);
  off++;
  iter->user_data = GINT_TO_POINTER (off);

  return off < self->items->len;
}

static gboolean
sysprof_log_model_iter_nth_child (GtkTreeModel *model,
                                  GtkTreeIter  *iter,
                                  GtkTreeIter  *parent,
                                  gint          n)
{
  SysprofLogModel *self = (SysprofLogModel *)model;

  g_assert (SYSPROF_IS_LOG_MODEL (self));
  g_assert (iter != NULL);

  if (parent != NULL)
    return FALSE;

  iter->user_data = GINT_TO_POINTER (n);

  return n < self->items->len;
}

static gint
sysprof_log_model_iter_n_children (GtkTreeModel *model,
                                   GtkTreeIter  *iter)
{
  SysprofLogModel *self = (SysprofLogModel *)model;

  g_assert (SYSPROF_IS_LOG_MODEL (self));

  return iter ? 0 : self->items->len;
}

static gboolean
sysprof_log_model_iter_has_child (GtkTreeModel *model,
                                  GtkTreeIter  *iter)
{
  return FALSE;
}

static GtkTreeModelFlags
sysprof_log_model_get_flags (GtkTreeModel *model)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}

static void
sysprof_log_model_get_value (GtkTreeModel *model,
                             GtkTreeIter  *iter,
                             gint          column,
                             GValue       *value)
{
  SysprofLogModel *self = (SysprofLogModel *)model;
  const Item *item;

  g_assert (SYSPROF_IS_LOG_MODEL (self));
  g_assert (iter != NULL);
  g_assert (column < SYSPROF_LOG_MODEL_COLUMN_LAST);

  item = &g_array_index (self->items, Item, GPOINTER_TO_INT (iter->user_data));

  switch (column)
    {
    case SYSPROF_LOG_MODEL_COLUMN_TIME_STRING:
      {
        gint64 offset = item->time - self->begin_time;
        gint min = offset / SYSPROF_NSEC_PER_SEC / 60L;
        gint seconds = ((offset - (min * SYSPROF_NSEC_PER_SEC)) / SYSPROF_NSEC_PER_SEC) % 60;
        gint msec = (offset % SYSPROF_NSEC_PER_SEC) / (SYSPROF_NSEC_PER_SEC / 1000L);

        g_value_init (value, G_TYPE_STRING);
        g_value_take_string (value,
                             g_strdup_printf ("%02d:%02d.%03d", min, seconds, msec));
      }
      break;

    case SYSPROF_LOG_MODEL_COLUMN_TIME:
      g_value_init (value, G_TYPE_INT64);
      g_value_set_int64 (value, item->time);
      break;

    case SYSPROF_LOG_MODEL_COLUMN_SEVERITY:
      g_value_init (value, G_TYPE_STRING);
      switch (item->severity)
        {
        case G_LOG_LEVEL_MESSAGE:
          g_value_set_static_string (value, _("Message"));
          break;
        case G_LOG_LEVEL_INFO:
          g_value_set_static_string (value, _("Info"));
          break;
        case G_LOG_LEVEL_CRITICAL:
          g_value_set_static_string (value, _("Critical"));
          break;
        case G_LOG_LEVEL_ERROR:
          g_value_set_static_string (value, _("Error"));
          break;
        case G_LOG_LEVEL_DEBUG:
          g_value_set_static_string (value, _("Debug"));
          break;
        case G_LOG_LEVEL_WARNING:
          g_value_set_static_string (value, _("Warning"));
          break;
        default:
          g_value_set_static_string (value, "");
          break;
        }
      break;

    case SYSPROF_LOG_MODEL_COLUMN_DOMAIN:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, item->domain);
      break;

    case SYSPROF_LOG_MODEL_COLUMN_MESSAGE:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, item->message);
      break;

    default:
      break;
    }
}

static void
tree_model_iface_init (GtkTreeModelIface *iface)
{
  iface->get_n_columns = sysprof_log_model_get_n_columns;
  iface->get_column_type = sysprof_log_model_get_column_type;
  iface->get_iter = sysprof_log_model_get_iter;
  iface->get_path = sysprof_log_model_get_path;
  iface->iter_next = sysprof_log_model_iter_next;
  iface->iter_n_children = sysprof_log_model_iter_n_children;
  iface->iter_nth_child = sysprof_log_model_iter_nth_child;
  iface->iter_has_child = sysprof_log_model_iter_has_child;
  iface->get_flags = sysprof_log_model_get_flags;
  iface->get_value = sysprof_log_model_get_value;
}

G_DEFINE_TYPE_WITH_CODE (SysprofLogModel, sysprof_log_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, tree_model_iface_init))

static void
sysprof_log_model_finalize (GObject *object)
{
  SysprofLogModel *self = (SysprofLogModel *)object;

  g_clear_pointer (&self->items, g_array_unref);
  g_clear_pointer (&self->chunks, g_string_chunk_free);

  G_OBJECT_CLASS (sysprof_log_model_parent_class)->finalize (object);
}

static void
sysprof_log_model_class_init (SysprofLogModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_log_model_finalize;
}

static void
sysprof_log_model_init (SysprofLogModel *self)
{
  self->chunks = g_string_chunk_new (4096*16);
  self->items = g_array_new (FALSE, FALSE, sizeof (Item));
}

static bool
cursor_foreach_cb (const SysprofCaptureFrame *frame,
                   gpointer                   user_data)
{
  SysprofLogModel *self = user_data;
  SysprofCaptureLog *log = (SysprofCaptureLog *)frame;
  Item item;

  g_assert (SYSPROF_IS_LOG_MODEL (self));
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_LOG);

  item.time = frame->time;
  item.severity = log->severity;
  item.domain = g_string_chunk_insert_const (self->chunks, log->domain);
  item.message = g_string_chunk_insert_const (self->chunks, log->message);

  g_array_append_val (self->items, item);

  return TRUE;
}

static gint
item_compare (gconstpointer a,
              gconstpointer b)
{
  const Item *ia = a;
  const Item *ib = b;

  if (ia->time < ib->time)
    return -1;
  else if (ia->time > ib->time)
    return 1;
  else
    return 0;
}

static void
sysprof_log_model_new_worker (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  g_autoptr(SysprofLogModel) self = NULL;
  SysprofCaptureCursor *cursor = task_data;
  SysprofCaptureReader *reader;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  self = g_object_new (SYSPROF_TYPE_LOG_MODEL, NULL);

  reader = sysprof_capture_cursor_get_reader (cursor);
  self->begin_time = sysprof_capture_reader_get_start_time (reader);

  sysprof_capture_cursor_foreach (cursor, cursor_foreach_cb, self);
  g_array_sort (self->items, item_compare);

  g_task_return_pointer (task, g_steal_pointer (&self), g_object_unref);
}

static void
sysprof_log_model_selection_foreach_cb (SysprofSelection *selection,
                                        gint64            begin,
                                        gint64            end,
                                        gpointer          user_data)
{
  SysprofCaptureCondition **condition = user_data;
  SysprofCaptureCondition *c;

  g_assert (SYSPROF_IS_SELECTION (selection));
  g_assert (condition != NULL);

  c = sysprof_capture_condition_new_where_time_between (begin, end);

  if (*condition != NULL)
    *condition = sysprof_capture_condition_new_or (g_steal_pointer (&c),
                                                   g_steal_pointer (condition));
  else
    *condition = g_steal_pointer (&c);
}

void
sysprof_log_model_new_async (SysprofCaptureReader *reader,
                             SysprofSelection     *selection,
                             GCancellable         *cancellable,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
  static const SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_LOG,
  };
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  SysprofCaptureCondition *c;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  cursor = sysprof_capture_cursor_new (reader);
  c = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);

  if (selection)
    {
      SysprofCaptureCondition *condition = NULL;

      sysprof_selection_foreach (selection,
                                 sysprof_log_model_selection_foreach_cb,
                                 &condition);
      if (condition)
        c = sysprof_capture_condition_new_and (c, g_steal_pointer (&condition));
    }

  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&c));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_log_model_new_async);
  g_task_set_task_data (task,
                        g_steal_pointer (&cursor),
                        (GDestroyNotify) sysprof_capture_cursor_unref);
  g_task_run_in_thread (task, sysprof_log_model_new_worker);
}

SysprofLogModel *
sysprof_log_model_new_finish (GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
