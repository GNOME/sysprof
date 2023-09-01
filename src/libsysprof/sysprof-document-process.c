/* sysprof-document-process.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"
#include "sysprof-document-process-private.h"
#include "sysprof-mount.h"
#include "sysprof-symbol-private.h"
#include "sysprof-thread-info.h"

struct _SysprofDocumentProcess
{
  SysprofDocumentFrame  parent_instance;
  SysprofProcessInfo   *process_info;
};

struct _SysprofDocumentProcessClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_COMMAND_LINE,
  PROP_DURATION,
  PROP_MEMORY_MAPS,
  PROP_MOUNTS,
  PROP_EXIT_TIME,
  PROP_THREADS,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentProcess, sysprof_document_process, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_process_finalize (GObject *object)
{
  SysprofDocumentProcess *self = (SysprofDocumentProcess *)object;

  g_clear_pointer (&self->process_info, sysprof_process_info_unref);

  G_OBJECT_CLASS (sysprof_document_process_parent_class)->finalize (object);
}

static void
sysprof_document_process_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofDocumentProcess *self = SYSPROF_DOCUMENT_PROCESS (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      g_value_set_string (value, sysprof_document_process_get_command_line (self));
      break;

    case PROP_DURATION:
      g_value_set_int64 (value, sysprof_document_process_get_duration (self));
      break;

    case PROP_EXIT_TIME:
      g_value_set_int64 (value, sysprof_document_process_get_exit_time (self));
      break;

    case PROP_MEMORY_MAPS:
      g_value_take_object (value, sysprof_document_process_list_memory_maps (self));
      break;

    case PROP_MOUNTS:
      g_value_take_object (value, sysprof_document_process_list_mounts (self));
      break;

    case PROP_THREADS:
      g_value_take_object (value, sysprof_document_process_list_threads (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, sysprof_document_process_dup_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_process_class_init (SysprofDocumentProcessClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_process_finalize;
  object_class->get_property = sysprof_document_process_get_property;

  properties [PROP_COMMAND_LINE] =
    g_param_spec_string ("command-line", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DURATION] =
    g_param_spec_int64 ("duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_EXIT_TIME] =
    g_param_spec_int64 ("exit-time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MEMORY_MAPS] =
    g_param_spec_object ("memory-maps", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MOUNTS] =
    g_param_spec_object ("mounts", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_THREADS] =
    g_param_spec_object ("threads", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_process_init (SysprofDocumentProcess *self)
{
}

const char *
_sysprof_document_process_get_comm (SysprofDocumentProcess *self)
{
  const SysprofCaptureProcess *proc;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), 0);

  proc = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureProcess);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, proc->cmdline);
}

const char *
sysprof_document_process_get_command_line (SysprofDocumentProcess *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), 0);

  /* Prefer ProcessInfo symbol name which gets updated from
   * subsequent comm[] updates for process records.
   */
  if (self->process_info != NULL &&
      self->process_info->symbol != NULL)
    return self->process_info->symbol->name;

  return _sysprof_document_process_get_comm (self);
}

/**
 * sysprof_document_process_list_memory_maps:
 * @self: a #SysprofDocumentProcess
 *
 * Lists the #SysprofDocumentMmap that are associated with the process.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentMmap
 */
GListModel *
sysprof_document_process_list_memory_maps (SysprofDocumentProcess *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), NULL);

  if (self->process_info == NULL)
    return G_LIST_MODEL (g_list_store_new (SYSPROF_TYPE_DOCUMENT_MMAP));

  return g_object_ref (G_LIST_MODEL (self->process_info->address_layout));
}

/**
 * sysprof_document_process_list_mounts:
 * @self: a #SysprofDocumentProcess
 *
 * Lists the #SysprofMount that are associated with the process.
 *
 * Returns: (transfer full): a #GListModel of #SysprofMount
 */
GListModel *
sysprof_document_process_list_mounts (SysprofDocumentProcess *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), NULL);

  if (self->process_info == NULL)
    return G_LIST_MODEL (g_list_store_new (SYSPROF_TYPE_MOUNT));

  return g_object_ref (G_LIST_MODEL (self->process_info->mount_namespace));
}

SysprofProcessInfo *
_sysprof_document_process_get_info (SysprofDocumentProcess *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), NULL);

  return self->process_info;
}

void
_sysprof_document_process_set_info (SysprofDocumentProcess *self,
                                    SysprofProcessInfo     *process_info)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self));
  g_return_if_fail (process_info != NULL);
  g_return_if_fail (self->process_info == NULL);

  self->process_info = sysprof_process_info_ref (process_info);
}

gint64
sysprof_document_process_get_exit_time (SysprofDocumentProcess *self)
{
  gint64 exit_time = 0;
  gint64 t;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), 0);

  if (self->process_info != NULL)
    exit_time = self->process_info->exit_time;

  t = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (self));

  return MAX (t, exit_time);
}

gint64
sysprof_document_process_get_duration (SysprofDocumentProcess *self)
{
  gint64 t;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), 0);

  t = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (self));

  return sysprof_document_process_get_exit_time (self) - t;
}

/**
 * sysprof_document_process_dup_title:
 * @self: a #SysprofDocumentProcess
 *
 * Gets a suitable title for the process.
 *
 * Returns: (transfer full): a string containing a process title
 */
char *
sysprof_document_process_dup_title (SysprofDocumentProcess *self)
{
  const char *command_line;
  int pid;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), NULL);

  pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (self));

  if ((command_line = sysprof_document_process_get_command_line (self)))
    return g_strdup_printf (_("%s [Process %d]"), command_line, pid);

  return g_strdup_printf (_("Process %d"), pid);
}

/**
 * sysprof_document_process_list_threads:
 * @self: a #SysprofDocumentProcess
 *
 * Gets the list of threads for the process.
 *
 * Returns: (transfer full): a #GListModel of #SysprofThreadInfo.
 */
GListModel *
sysprof_document_process_list_threads (SysprofDocumentProcess *self)
{
  GListStore *store;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), NULL);

  store = g_list_store_new (SYSPROF_TYPE_THREAD_INFO);

  if (self->process_info != NULL)
    {
      g_autoptr(GPtrArray) threads = g_ptr_array_new_with_free_func (g_object_unref);
      EggBitsetIter iter;
      guint i;

      if (egg_bitset_iter_init_first (&iter, self->process_info->thread_ids, &i))
        {
          do
            {
              g_ptr_array_add (threads,
                               g_object_new (SYSPROF_TYPE_THREAD_INFO,
                                             "process", self,
                                             "thread-id", i,
                                             NULL));
            }
          while (egg_bitset_iter_next (&iter, &i));
        }

      if (threads->len > 0)
        g_list_store_splice (store, 0, 0, threads->pdata, threads->len);
    }

  return G_LIST_MODEL (store);
}
