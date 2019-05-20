/* sysprof-process-model.c
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

#include "sysprof-backport-autocleanups.h"
#include "sysprof-helpers.h"
#include "sysprof-process-model.h"
#include "sysprof-process-model-item.h"

#define QUEUE_RELOAD_TIMEOUT_MSEC 100

struct _SysprofProcessModel
{
  GObject    parent_instance;
  GPtrArray *items;
  guint      reload_source;
  guint      no_proxy : 1;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofProcessModel, sysprof_process_model, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_process_model_finalize (GObject *object)
{
  SysprofProcessModel *self = (SysprofProcessModel *)object;

  g_clear_handle_id (&self->reload_source, g_source_remove);
  g_clear_pointer (&self->items, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_process_model_parent_class)->finalize (object);
}

static void
sysprof_process_model_class_init (SysprofProcessModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_process_model_finalize;
}

static void
sysprof_process_model_init (SysprofProcessModel *self)
{
  self->items = g_ptr_array_new_with_free_func (g_object_unref);
}

static guint
find_index (GPtrArray *ar,
            GPid       pid)
{
  guint i;

  g_assert (ar != NULL);

  for (i = 0; i < ar->len; i++)
    {
      SysprofProcessModelItem *item = g_ptr_array_index (ar, i);
      GPid item_pid = sysprof_process_model_item_get_pid (item);

      g_assert (pid != item_pid);

      if (item_pid > pid)
        return i;
    }

  return ar->len;
}

static void
sysprof_process_model_merge_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  SysprofProcessModel *self = (SysprofProcessModel *)object;
  g_autoptr(GPtrArray) ret = NULL;
  g_autoptr(GHashTable) old_hash = NULL;
  g_autoptr(GHashTable) new_hash = NULL;
  GError *error = NULL;
  guint i;

  g_assert (SYSPROF_IS_PROCESS_MODEL (self));
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

  old_hash = g_hash_table_new ((GHashFunc)sysprof_process_model_item_hash,
                               (GEqualFunc)sysprof_process_model_item_equal);
  new_hash = g_hash_table_new ((GHashFunc)sysprof_process_model_item_hash,
                               (GEqualFunc)sysprof_process_model_item_equal);

  for (i = 0; i < self->items->len; i++)
    {
      SysprofProcessModelItem *item = g_ptr_array_index (self->items, i);

      g_hash_table_insert (old_hash, item, NULL);
    }

  for (i = 0; i < ret->len; i++)
    {
      SysprofProcessModelItem *item = g_ptr_array_index (ret, i);

      g_hash_table_insert (new_hash, item, NULL);
    }

  for (i = self->items->len; i > 0; i--)
    {
      guint index = i - 1;
      SysprofProcessModelItem *item = g_ptr_array_index (self->items, index);

      if (!g_hash_table_contains (new_hash, item))
        {
          g_ptr_array_remove_index (self->items, index);
          g_list_model_items_changed (G_LIST_MODEL (self), index, 1, 0);
        }
    }

  for (i = 0; i < ret->len; i++)
    {
      SysprofProcessModelItem *item = g_ptr_array_index (ret, i);
      GPid pid;
      guint index;

      if (g_hash_table_contains (old_hash, item))
        continue;

      pid = sysprof_process_model_item_get_pid (item);
      index = find_index (self->items, pid);

      g_ptr_array_insert (self->items, index, g_object_ref (item));
      g_list_model_items_changed (G_LIST_MODEL (self), index, 0, 1);
    }
}

static gint
compare_by_pid (gconstpointer a,
                gconstpointer b)
{
  SysprofProcessModelItem **aitem = (SysprofProcessModelItem **)a;
  SysprofProcessModelItem **bitem = (SysprofProcessModelItem **)b;

  return sysprof_process_model_item_get_pid (*aitem) - sysprof_process_model_item_get_pid (*bitem);
}

static void
sysprof_process_model_reload_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  SysprofProcessModel *self = source_object;
  SysprofHelpers *helpers = sysprof_helpers_get_default ();
  g_autoptr(GPtrArray) ret = NULL;
  g_autoptr(GVariant) info = NULL;

  g_assert (SYSPROF_IS_PROCESS_MODEL (source_object));
  g_assert (G_IS_TASK (task));

  ret = g_ptr_array_new_with_free_func (g_object_unref);

  if (sysprof_helpers_get_process_info (helpers, "pid,cmdline,comm", self->no_proxy, NULL, &info, NULL))
    {
      gsize n_children = g_variant_n_children (info);

      for (gsize i = 0; i < n_children; i++)
        {
          g_autoptr(GVariant) pidinfo = g_variant_get_child_value (info, i);
          g_autoptr(SysprofProcessModelItem) item = sysprof_process_model_item_new_from_variant (pidinfo);

          if (sysprof_process_model_item_is_kernel (item))
            continue;

          g_ptr_array_add (ret, g_steal_pointer (&item));
        }

      g_ptr_array_sort (ret, compare_by_pid);
    }

  g_task_return_pointer (task,
                         g_steal_pointer (&ret),
                         (GDestroyNotify)g_ptr_array_unref);
}

static gboolean
sysprof_process_model_do_reload (gpointer user_data)
{
  SysprofProcessModel *self = user_data;
  g_autoptr(GTask) task = NULL;

  g_clear_handle_id (&self->reload_source, g_source_remove);

  task = g_task_new (self, NULL, sysprof_process_model_merge_cb, NULL);
  g_task_set_priority (task, G_PRIORITY_LOW);
  g_task_run_in_thread (task, sysprof_process_model_reload_worker);

  return G_SOURCE_REMOVE;
}

SysprofProcessModel *
sysprof_process_model_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROCESS_MODEL, NULL);
}

void
sysprof_process_model_reload (SysprofProcessModel *self)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_PROCESS_MODEL (self));

  g_clear_handle_id (&self->reload_source, g_source_remove);

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_priority (task, G_PRIORITY_LOW);
  g_task_run_in_thread_sync (task, sysprof_process_model_reload_worker);

  sysprof_process_model_merge_cb (G_OBJECT (self), G_ASYNC_RESULT (task), NULL);
}

void
sysprof_process_model_queue_reload (SysprofProcessModel *self)
{
  g_return_if_fail (SYSPROF_IS_PROCESS_MODEL (self));

  if (self->reload_source == 0)
    self->reload_source = g_timeout_add (QUEUE_RELOAD_TIMEOUT_MSEC,
                                         sysprof_process_model_do_reload,
                                         self);
}

static GType
sysprof_process_model_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_PROCESS_MODEL_ITEM;
}

static guint
sysprof_process_model_get_n_items (GListModel *model)
{
  SysprofProcessModel *self = (SysprofProcessModel *)model;

  return self->items->len;
}

static gpointer
sysprof_process_model_get_item (GListModel *model,
                                guint       position)
{
  SysprofProcessModel *self = (SysprofProcessModel *)model;

  g_return_val_if_fail (SYSPROF_IS_PROCESS_MODEL (self), NULL);
  g_return_val_if_fail (position < self->items->len, NULL);

  return g_object_ref (g_ptr_array_index (self->items, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_process_model_get_item_type;
  iface->get_n_items = sysprof_process_model_get_n_items;
  iface->get_item = sysprof_process_model_get_item;
}

void
sysprof_process_model_set_no_proxy (SysprofProcessModel *self,
                                    gboolean             no_proxy)
{
  g_return_if_fail (SYSPROF_IS_PROCESS_MODEL (self));

  self->no_proxy = !!no_proxy;
}
