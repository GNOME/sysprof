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

#include "sysprof-marks-model.h"

struct _SysprofMarksModel
{
  GObject       parent_instance;
  GStringChunk *chunks;
  GArray       *items;
};

typedef struct
{
  gint64       begin_time;
  gint64       end_time;
  const gchar *group;
  const gchar *name;
} Item;

static void
tree_model_iface_init (GtkTreeModelIface *iface)
{
}

G_DEFINE_TYPE_WITH_CODE (SysprofMarksModel, sysprof_marks_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, tree_model_iface_init))

static void
sysprof_marks_model_finalize (GObject *object)
{
  SysprofMarksModel *self = (SysprofMarksModel *)object;

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
  self->chunks = g_string_chunk_new (4096*16);
  self->items = g_array_new (FALSE, FALSE, sizeof (Item));
}

static gboolean
cursor_foreach_cb (const SysprofCaptureFrame *frame,
                   gpointer                   user_data)
{
  SysprofMarksModel *self = user_data;
  SysprofCaptureMark *mark = (SysprofCaptureMark *)frame;
  Item item;

  g_assert (SYSPROF_IS_MARKS_MODEL (self));
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_MARK);

  item.begin_time = frame->time;
  item.end_time = item.begin_time + mark->duration;
  item.group = g_string_chunk_insert_const (self->chunks, mark->group);
  item.name = g_string_chunk_insert_const (self->chunks, mark->name);

  g_array_append_val (self->items, item);

  return FALSE;
}

static void
sysprof_marks_model_new_worker (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
  const SysprofCaptureFrameType types[] = { SYSPROF_CAPTURE_FRAME_MARK };
  SysprofCaptureReader *reader = task_data;
  g_autoptr(SysprofCaptureCondition) condition = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(SysprofMarksModel) self = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  self = g_object_new (SYSPROF_TYPE_MARKS_MODEL, NULL);

  cursor = sysprof_capture_cursor_new (reader);
  condition = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  sysprof_capture_cursor_add_condition (cursor, condition);
  sysprof_capture_cursor_foreach (cursor, cursor_foreach_cb, self);

  g_task_return_pointer (task, g_steal_pointer (&self), g_object_unref);
}

void
sysprof_marks_model_new_async (SysprofCaptureReader *reader,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_marks_model_new_async);
  g_task_set_task_data (task,
                        sysprof_capture_reader_copy (reader),
                        (GDestroyNotify) sysprof_capture_reader_unref);
  g_task_run_in_thread (task, sysprof_marks_model_new_worker);
}

SysprofMarksModel *
sysprof_marks_model_new_finish (GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
