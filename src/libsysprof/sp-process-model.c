/* sp-process-model.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#include <stdlib.h>

#include "sp-process-model.h"
#include "sp-process-model-item.h"

#define QUEUE_RELOAD_TIMEOUT_MSEC 100

struct _SpProcessModel
{
  GObject    parent_instance;
  guint      reload_source;
  GPtrArray *items;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpProcessModel, sp_process_model, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sp_process_model_finalize (GObject *object)
{
  SpProcessModel *self = (SpProcessModel *)object;

  if (self->reload_source)
    {
      g_source_remove (self->reload_source);
      self->reload_source = 0;
    }

  g_clear_pointer (&self->items, g_ptr_array_unref);

  G_OBJECT_CLASS (sp_process_model_parent_class)->finalize (object);
}

static void
sp_process_model_class_init (SpProcessModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_process_model_finalize;
}

static void
sp_process_model_init (SpProcessModel *self)
{
  self->items = g_ptr_array_new_with_free_func (g_object_unref);

  sp_process_model_queue_reload (self);
}

static guint
find_index (GPtrArray *ar,
            GPid       pid)
{
  guint i;

  g_assert (ar != NULL);

  for (i = 0; i < ar->len; i++)
    {
      SpProcessModelItem *item = g_ptr_array_index (ar, i);
      GPid item_pid = sp_process_model_item_get_pid (item);

      g_assert (pid != item_pid);

      if (item_pid > pid)
        return i;
    }

  return ar->len;
}

static void
sp_process_model_merge_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  SpProcessModel *self = (SpProcessModel *)object;
  g_autoptr(GPtrArray) ret = NULL;
  g_autoptr(GHashTable) old_hash = NULL;
  g_autoptr(GHashTable) new_hash = NULL;
  GError *error = NULL;
  guint i;

  g_assert (SP_IS_PROCESS_MODEL (self));
  g_assert (G_IS_TASK (result));

  ret = g_task_propagate_pointer (G_TASK (result), &error);

  if (ret == NULL)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return;
    }

  /*
   * TODO: Clearly this could be optimized to walk both arrays at once
   *       and do a proper 2-way merge.
   */

  old_hash = g_hash_table_new ((GHashFunc)sp_process_model_item_hash,
                               (GEqualFunc)sp_process_model_item_equal);
  new_hash = g_hash_table_new ((GHashFunc)sp_process_model_item_hash,
                               (GEqualFunc)sp_process_model_item_equal);

  for (i = 0; i < self->items->len; i++)
    {
      SpProcessModelItem *item = g_ptr_array_index (self->items, i);

      g_hash_table_insert (old_hash, item, NULL);
    }

  for (i = 0; i < ret->len; i++)
    {
      SpProcessModelItem *item = g_ptr_array_index (ret, i);

      g_hash_table_insert (new_hash, item, NULL);
    }

  for (i = self->items->len; i > 0; i--)
    {
      guint index = i - 1;
      SpProcessModelItem *item = g_ptr_array_index (self->items, index);

      if (!g_hash_table_contains (new_hash, item))
        {
          g_ptr_array_remove_index (self->items, index);
          g_list_model_items_changed (G_LIST_MODEL (self), index, 1, 0);
        }
    }

  for (i = 0; i < ret->len; i++)
    {
      SpProcessModelItem *item = g_ptr_array_index (ret, i);
      GPid pid;
      guint index;

      if (g_hash_table_contains (old_hash, item))
        continue;

      pid = sp_process_model_item_get_pid (item);
      index = find_index (self->items, pid);

      g_ptr_array_insert (self->items, index, g_object_ref (item));
      g_list_model_items_changed (G_LIST_MODEL (self), index, 0, 1);
    }
}

static gint
compare_by_pid (gconstpointer a,
                gconstpointer b)
{
  SpProcessModelItem **aitem = (SpProcessModelItem **)a;
  SpProcessModelItem **bitem = (SpProcessModelItem **)b;

  return sp_process_model_item_get_pid (*aitem) - sp_process_model_item_get_pid (*bitem);
}

static void
sp_process_model_reload_worker (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
  g_autoptr(GPtrArray) ret = NULL;
  const gchar *name;
  GError *error = NULL;
  GDir *dir;

  g_assert (SP_IS_PROCESS_MODEL (source_object));
  g_assert (G_IS_TASK (task));

  dir = g_dir_open ("/proc", 0, &error);

  if (dir == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  ret = g_ptr_array_new_with_free_func (g_object_unref);

  while ((name = g_dir_read_name (dir)))
    {
      SpProcessModelItem *item;
      GPid pid;
      gchar *end;

      pid = strtol (name, &end, 10);
      if (pid <= 0 || *end != '\0')
        continue;

      item = sp_process_model_item_new (pid);

      if (sp_process_model_item_is_kernel (item))
        {
          g_object_unref (item);
          continue;
        }

      g_ptr_array_add (ret, item);
    }

  g_dir_close (dir);

  g_ptr_array_sort (ret, compare_by_pid);
  g_task_return_pointer (task, g_ptr_array_ref (ret), (GDestroyNotify)g_ptr_array_unref);
}

static gboolean
sp_process_model_do_reload (gpointer user_data)
{
  SpProcessModel *self = user_data;
  g_autoptr(GTask) task = NULL;

  self->reload_source = 0;

  task = g_task_new (self, NULL, sp_process_model_merge_cb, NULL);
  g_task_set_priority (task, G_PRIORITY_LOW);
  g_task_run_in_thread (task, sp_process_model_reload_worker);

  return G_SOURCE_REMOVE;
}

SpProcessModel *
sp_process_model_new (void)
{
  return g_object_new (SP_TYPE_PROCESS_MODEL, NULL);
}

void
sp_process_model_reload (SpProcessModel *self)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SP_IS_PROCESS_MODEL (self));

  if (self->reload_source != 0)
    {
      g_source_remove (self->reload_source);
      self->reload_source = 0;
    }

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_priority (task, G_PRIORITY_LOW);
  g_task_run_in_thread_sync (task, sp_process_model_reload_worker);

  sp_process_model_merge_cb (G_OBJECT (self), G_ASYNC_RESULT (task), NULL);
}

void
sp_process_model_queue_reload (SpProcessModel *self)
{
  g_return_if_fail (SP_IS_PROCESS_MODEL (self));

  if (self->reload_source == 0)
    self->reload_source = g_timeout_add (QUEUE_RELOAD_TIMEOUT_MSEC,
                                         sp_process_model_do_reload,
                                         self);
}

static GType
sp_process_model_get_item_type (GListModel *model)
{
  return SP_TYPE_PROCESS_MODEL_ITEM;
}

static guint
sp_process_model_get_n_items (GListModel *model)
{
  SpProcessModel *self = (SpProcessModel *)model;

  return self->items->len;
}

static gpointer
sp_process_model_get_item (GListModel *model,
                           guint       position)
{
  SpProcessModel *self = (SpProcessModel *)model;

  g_return_val_if_fail (SP_IS_PROCESS_MODEL (self), NULL);
  g_return_val_if_fail (position < self->items->len, NULL);

  return g_object_ref (g_ptr_array_index (self->items, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sp_process_model_get_item_type;
  iface->get_n_items = sp_process_model_get_n_items;
  iface->get_item = sp_process_model_get_item;
}
