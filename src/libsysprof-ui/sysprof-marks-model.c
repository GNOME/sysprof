/* sysprof-marks-model.c
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

#define G_LOG_DOMAIN "sysprof-marks-model"

#include "config.h"

#include <string.h>

#include "sysprof-marks-model.h"

struct _SysprofMarksModel
{
  GObject       parent_instance;
  GStringChunk *chunks;
  GHashTable   *counters;
  GArray       *items;
  gint64        max_end_time;
};

typedef struct
{
  gint64                      begin_time;
  gint64                      end_time;
  const gchar                *group;
  const gchar                *name;
  const gchar                *message;
  SysprofCaptureCounterValue  value;
  guint                       is_counter : 1;
  guint                       counter_type : 8;
} Item;

static void
counter_free (gpointer data)
{
  g_slice_free (SysprofCaptureCounter, data);
}

static gint
sysprof_marks_model_get_n_columns (GtkTreeModel *model)
{
  return SYSPROF_MARKS_MODEL_COLUMN_LAST;
}

static GType
sysprof_marks_model_get_column_type (GtkTreeModel *model,
                                     gint          column)
{
  switch (column)
    {
    case SYSPROF_MARKS_MODEL_COLUMN_GROUP:
      return G_TYPE_STRING;

    case SYSPROF_MARKS_MODEL_COLUMN_NAME:
      return G_TYPE_STRING;

    case SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME:
      return G_TYPE_INT64;

    case SYSPROF_MARKS_MODEL_COLUMN_END_TIME:
      return G_TYPE_INT64;

    case SYSPROF_MARKS_MODEL_COLUMN_DURATION:
      return G_TYPE_DOUBLE;

    case SYSPROF_MARKS_MODEL_COLUMN_TEXT:
      return G_TYPE_STRING;

    default:
      return 0;
    }
}

static GtkTreePath *
sysprof_marks_model_get_path (GtkTreeModel *model,
                              GtkTreeIter  *iter)
{
  gint off;

  g_assert (SYSPROF_IS_MARKS_MODEL (model));
  g_assert (iter != NULL);

  off = GPOINTER_TO_INT (iter->user_data);

  return gtk_tree_path_new_from_indices (off, -1);
}

static gboolean
sysprof_marks_model_get_iter (GtkTreeModel *model,
                              GtkTreeIter  *iter,
                              GtkTreePath  *path)
{
  SysprofMarksModel *self = (SysprofMarksModel *)model;
  gint off;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
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
sysprof_marks_model_iter_next (GtkTreeModel *model,
                               GtkTreeIter  *iter)
{
  SysprofMarksModel *self = (SysprofMarksModel *)model;
  gint off;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
  g_assert (iter != NULL);

  off = GPOINTER_TO_INT (iter->user_data);
  off++;
  iter->user_data = GINT_TO_POINTER (off);

  return off < self->items->len;
}

static gboolean
sysprof_marks_model_iter_nth_child (GtkTreeModel *model,
                                    GtkTreeIter  *iter,
                                    GtkTreeIter  *parent,
                                    gint          n)
{
  SysprofMarksModel *self = (SysprofMarksModel *)model;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
  g_assert (iter != NULL);

  if (parent != NULL)
    return FALSE;

  iter->user_data = GINT_TO_POINTER (n);

  return n < self->items->len;
}

static gint
sysprof_marks_model_iter_n_children (GtkTreeModel *model,
                                     GtkTreeIter  *iter)
{
  SysprofMarksModel *self = (SysprofMarksModel *)model;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));

  return iter ? 0 : self->items->len;
}

static gboolean
sysprof_marks_model_iter_has_child (GtkTreeModel *model,
                                    GtkTreeIter  *iter)
{
  return FALSE;
}

static GtkTreeModelFlags
sysprof_marks_model_get_flags (GtkTreeModel *model)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}

static void
sysprof_marks_model_get_value (GtkTreeModel *model,
                               GtkTreeIter  *iter,
                               gint          column,
                               GValue       *value)
{
  SysprofMarksModel *self = (SysprofMarksModel *)model;
  const Item *item;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
  g_assert (iter != NULL);
  g_assert (column < SYSPROF_MARKS_MODEL_COLUMN_LAST);

  item = &g_array_index (self->items, Item, GPOINTER_TO_INT (iter->user_data));

  switch (column)
    {
    case SYSPROF_MARKS_MODEL_COLUMN_GROUP:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, item->group);
      break;

    case SYSPROF_MARKS_MODEL_COLUMN_NAME:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, item->name);
      break;

    case SYSPROF_MARKS_MODEL_COLUMN_BEGIN_TIME:
      g_value_init (value, G_TYPE_INT64);
      g_value_set_int64 (value, item->begin_time);
      break;

    case SYSPROF_MARKS_MODEL_COLUMN_END_TIME:
      g_value_init (value, G_TYPE_INT64);
      g_value_set_int64 (value, item->end_time);
      break;

    case SYSPROF_MARKS_MODEL_COLUMN_DURATION:
      g_value_init (value, G_TYPE_DOUBLE);
      if (item->end_time)
        g_value_set_double (value, (item->end_time - item->begin_time) / (double)(G_USEC_PER_SEC * 1000));
      break;

    case SYSPROF_MARKS_MODEL_COLUMN_TEXT:
      g_value_init (value, G_TYPE_STRING);
      if (item->is_counter)
        {
          gchar *val = NULL;

          if (item->counter_type == SYSPROF_CAPTURE_COUNTER_DOUBLE)
            val = g_strdup_printf ("%s — %s = %.4lf", item->group, item->name, item->value.vdbl);
          else if (item->counter_type == SYSPROF_CAPTURE_COUNTER_INT64)
            val = g_strdup_printf ("%s — %s = %"G_GINT64_FORMAT, item->group, item->name, item->value.v64);

          g_value_take_string (value, g_steal_pointer (&val));
        }
      else
        {
          if (item->message && item->message[0])
            g_value_take_string (value, g_strdup_printf ("%s — %s", item->name, item->message));
          else
            g_value_set_string (value, item->name);
        }
      break;

    default:
      break;
    }
}

static void
tree_model_iface_init (GtkTreeModelIface *iface)
{
  iface->get_n_columns = sysprof_marks_model_get_n_columns;
  iface->get_column_type = sysprof_marks_model_get_column_type;
  iface->get_iter = sysprof_marks_model_get_iter;
  iface->get_path = sysprof_marks_model_get_path;
  iface->iter_next = sysprof_marks_model_iter_next;
  iface->iter_n_children = sysprof_marks_model_iter_n_children;
  iface->iter_nth_child = sysprof_marks_model_iter_nth_child;
  iface->iter_has_child = sysprof_marks_model_iter_has_child;
  iface->get_flags = sysprof_marks_model_get_flags;
  iface->get_value = sysprof_marks_model_get_value;
}

G_DEFINE_TYPE_WITH_CODE (SysprofMarksModel, sysprof_marks_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, tree_model_iface_init))

static void
sysprof_marks_model_finalize (GObject *object)
{
  SysprofMarksModel *self = (SysprofMarksModel *)object;

  g_clear_pointer (&self->counters, g_hash_table_unref);
  g_clear_pointer (&self->items, g_array_unref);
  g_clear_pointer (&self->chunks, g_string_chunk_free);

  G_OBJECT_CLASS (sysprof_marks_model_parent_class)->finalize (object);
}

static void
sysprof_marks_model_class_init (SysprofMarksModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_marks_model_finalize;
}

static void
sysprof_marks_model_init (SysprofMarksModel *self)
{
  self->counters = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, counter_free);
  self->chunks = g_string_chunk_new (4096*16);
  self->items = g_array_new (FALSE, FALSE, sizeof (Item));
}

static bool
cursor_foreach_cb (const SysprofCaptureFrame *frame,
                   gpointer                   user_data)
{
  SysprofMarksModel *self = user_data;
  Item item;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_MARK ||
            frame->type == SYSPROF_CAPTURE_FRAME_CTRSET ||
            frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF ||
            frame->type == SYSPROF_CAPTURE_FRAME_FORK);

  if (frame->type == SYSPROF_CAPTURE_FRAME_MARK)
    {
      SysprofCaptureMark *mark = (SysprofCaptureMark *)frame;

      item.begin_time = frame->time;
      item.end_time = item.begin_time + mark->duration;
      item.group = g_string_chunk_insert_const (self->chunks, mark->group);
      item.name = g_string_chunk_insert_const (self->chunks, mark->name);
      item.message = g_string_chunk_insert_const (self->chunks, mark->message);
      item.value.v64 = 0;
      item.is_counter = FALSE;
      item.counter_type = 0;

      if G_LIKELY (item.end_time > self->max_end_time)
        self->max_end_time = item.end_time;

      g_array_append_val (self->items, item);
    }
  else if (frame->type == SYSPROF_CAPTURE_FRAME_FORK)
    {
      SysprofCaptureFork *fk = (SysprofCaptureFork *)frame;
      g_autofree gchar *message = g_strdup_printf ("PID: %d, Child PID: %d", frame->pid, fk->child_pid);

      item.begin_time = frame->time;
      item.end_time = item.begin_time;
      item.group = g_string_chunk_insert_const (self->chunks, "fork");
      item.name = g_string_chunk_insert_const (self->chunks, "Fork");
      item.message = g_string_chunk_insert_const (self->chunks, message);
      item.value.v64 = 0;
      item.is_counter = FALSE;
      item.counter_type = 0;

      g_array_append_val (self->items, item);
    }
  else if (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF)
    {
      SysprofCaptureCounterDefine *ctrdef = (SysprofCaptureCounterDefine *)frame;

      for (guint i = 0; i < ctrdef->n_counters; i++)
        {
          SysprofCaptureCounter *ctr = &ctrdef->counters[i];

          g_hash_table_insert (self->counters,
                               GUINT_TO_POINTER ((guint)ctr->id),
                               g_slice_dup (SysprofCaptureCounter, ctr));
        }
    }
  else if (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET)
    {
      SysprofCaptureCounterSet *ctrset = (SysprofCaptureCounterSet *)frame;

      for (guint i = 0; i < ctrset->n_values; i++)
        {
          SysprofCaptureCounterValues *values = &ctrset->values[i];

          for (guint j = 0; j < G_N_ELEMENTS (values->ids); j++)
            {
              guint32 id = values->ids[j];
              SysprofCaptureCounter *ctr = NULL;

              if (id == 0)
                break;

              if ((ctr = g_hash_table_lookup (self->counters, GUINT_TO_POINTER (id))))
                {
                  item.begin_time = frame->time;
                  item.end_time = frame->time;
                  item.group = ctr->category;
                  item.name = ctr->name;
                  item.message = NULL;
                  item.is_counter = TRUE;
                  item.counter_type = ctr->type;

                  memcpy (&item.value, &values->values[j], sizeof item.value);

                  g_array_append_val (self->items, item);
                }
            }

        }
    }

  return TRUE;
}

static gint
item_compare (gconstpointer a,
              gconstpointer b)
{
  const Item *ia = a;
  const Item *ib = b;

  if (ia->begin_time < ib->begin_time)
    return -1;
  else if (ia->begin_time > ib->begin_time)
    return 1;

  /* Sort items with longer duration first, as they might be
   * "overarching" marks containing other marks.
   */
  if (ia->end_time > ib->end_time)
    return -1;
  else if (ib->end_time > ia->end_time)
    return 1;

  return 0;
}

static void
sysprof_marks_model_new_worker (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
  g_autoptr(SysprofMarksModel) self = NULL;
  SysprofCaptureCursor *cursor = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  self = g_object_new (SYSPROF_TYPE_MARKS_MODEL, NULL);
  sysprof_capture_cursor_foreach (cursor, cursor_foreach_cb, self);
  g_array_sort (self->items, item_compare);

  g_task_return_pointer (task, g_steal_pointer (&self), g_object_unref);
}

static void
sysprof_marks_model_selection_foreach_cb (SysprofSelection *selection,
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
sysprof_marks_model_new_async (SysprofCaptureReader  *reader,
                               SysprofMarksModelKind  kind,
                               SysprofSelection      *selection,
                               GCancellable          *cancellable,
                               GAsyncReadyCallback    callback,
                               gpointer               user_data)
{
  static const SysprofCaptureFrameType ctrset[] = { SYSPROF_CAPTURE_FRAME_CTRDEF };
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  SysprofCaptureCondition *c;

  g_return_if_fail (reader != NULL);
  g_return_if_fail (!selection || SYSPROF_IS_SELECTION (selection));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  cursor = sysprof_capture_cursor_new (reader);

  if (kind == SYSPROF_MARKS_MODEL_BOTH)
    {
      static const SysprofCaptureFrameType types[] = {
        SYSPROF_CAPTURE_FRAME_CTRSET,
        SYSPROF_CAPTURE_FRAME_MARK,
      };

      c = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
    }
  else if (kind == SYSPROF_MARKS_MODEL_MARKS)
    {
      static const SysprofCaptureFrameType types[] = {
        SYSPROF_CAPTURE_FRAME_MARK,
        SYSPROF_CAPTURE_FRAME_FORK,
      };

      c = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
    }
  else if (kind == SYSPROF_MARKS_MODEL_COUNTERS)
    {
      static const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_CTRSET };

      c = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
    }
  else
    {
      g_task_report_new_error (NULL, callback, user_data,
                               sysprof_marks_model_new_async,
                               G_IO_ERROR,
                               G_IO_ERROR_INVAL,
                               "Invalid arguments");
      return;
    }

  if (selection)
    {
      SysprofCaptureCondition *condition = NULL;

      sysprof_selection_foreach (selection,
                                 sysprof_marks_model_selection_foreach_cb,
                                 &condition);
      if (condition)
        c = sysprof_capture_condition_new_and (c, g_steal_pointer (&condition));
    }

  if (kind & SYSPROF_MARKS_MODEL_COUNTERS)
    {
      c = sysprof_capture_condition_new_or (
        sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (ctrset), ctrset),
        g_steal_pointer (&c));
    }

  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&c));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_marks_model_new_async);
  g_task_set_task_data (task,
                        g_steal_pointer (&cursor),
                        (GDestroyNotify) sysprof_capture_cursor_unref);
  g_task_run_in_thread (task, sysprof_marks_model_new_worker);
}

SysprofMarksModel *
sysprof_marks_model_new_finish (GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
sysprof_marks_model_get_range (SysprofMarksModel *self,
                               gint64            *begin_time,
                               gint64            *end_time)
{
  g_return_if_fail (SYSPROF_IS_MARKS_MODEL (self));

  if (begin_time != NULL)
    {
      *begin_time = 0;

      if (self->items->len > 0)
        *begin_time = g_array_index (self->items, Item, 0).begin_time;
    }

  if (end_time != NULL)
    *end_time = self->max_end_time;
}

GType
sysprof_marks_model_kind_get_type (void)
{
  static GType type_id;

  if (g_once_init_enter (&type_id))
    {
      static const GEnumValue values[] = {
        { SYSPROF_MARKS_MODEL_MARKS, "SYSPROF_MARKS_MODEL_MARKS", "marks" },
        { SYSPROF_MARKS_MODEL_COUNTERS, "SYSPROF_MARKS_MODEL_COUNTERS", "counters" },
        { SYSPROF_MARKS_MODEL_BOTH, "SYSPROF_MARKS_MODEL_BOTH", "both" },
        { 0 },
      };
      GType _type_id = g_enum_register_static ("SysprofMarksModelKind", values);
      g_once_init_leave (&type_id, _type_id);
    }

  return type_id;
}
