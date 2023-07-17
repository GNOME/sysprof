/* sysprof-thread-info.c
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

#include "sysprof-thread-info.h"

struct _SysprofThreadInfo
{
  GObject parent_instance;
  SysprofDocumentProcess *process;
  int thread_id;
};

enum {
  PROP_0,
  PROP_PROCESS,
  PROP_THREAD_ID,
  PROP_IS_MAIN_THREAD,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofThreadInfo, sysprof_thread_info, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_thread_info_finalize (GObject *object)
{
  SysprofThreadInfo *self = (SysprofThreadInfo *)object;

  g_clear_object (&self->process);

  G_OBJECT_CLASS (sysprof_thread_info_parent_class)->finalize (object);
}

static void
sysprof_thread_info_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofThreadInfo *self = SYSPROF_THREAD_INFO (object);

  switch (prop_id)
    {
    case PROP_IS_MAIN_THREAD:
      g_value_set_boolean (value, sysprof_thread_info_is_main_thread (self));
      break;

    case PROP_PROCESS:
      g_value_set_object (value, sysprof_thread_info_get_process (self));
      break;

    case PROP_THREAD_ID:
      g_value_set_int (value, sysprof_thread_info_get_thread_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_thread_info_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofThreadInfo *self = SYSPROF_THREAD_INFO (object);

  switch (prop_id)
    {
    case PROP_PROCESS:
      self->process = g_value_dup_object (value);
      break;

    case PROP_THREAD_ID:
      self->thread_id = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_thread_info_class_init (SysprofThreadInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_thread_info_finalize;
  object_class->get_property = sysprof_thread_info_get_property;
  object_class->set_property = sysprof_thread_info_set_property;

  properties[PROP_IS_MAIN_THREAD] =
    g_param_spec_boolean ("is-main-thread", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PROCESS] =
    g_param_spec_object ("process", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_PROCESS,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_THREAD_ID] =
    g_param_spec_int ("thread-id", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_thread_info_init (SysprofThreadInfo *self)
{
}

/**
 * sysprof_thread_info_get_process:
 * @self: a #SysprofThreadInfo
 *
 * Gets the process that owns the thread info.
 *
 * Returns: (transfer none): a #SysprofDocumentProcess
 */
SysprofDocumentProcess *
sysprof_thread_info_get_process (SysprofThreadInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_THREAD_INFO (self), NULL);

  return self->process;
}

/**
 * sysprof_thread_info_get_thread_id:
 * @self: a #SysprofThreadInfo
 *
 * Gets the thread identifier.
 *
 * This typically matches what `gettid()` syscall returns on Linux.
 *
 * Returns: an integer containing the thread-id
 */
int
sysprof_thread_info_get_thread_id (SysprofThreadInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_THREAD_INFO (self), -1);

  return self->thread_id;
}

/**
 * sysprof_thread_info_is_main_thread:
 * @self: a #SysprofThreadInfo
 *
 * Checks if the thread is the main thread for a process.
 *
 * Returns: %TRUE if the thread-id is the main thread.
 */
gboolean
sysprof_thread_info_is_main_thread (SysprofThreadInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_THREAD_INFO (self), FALSE);

  return self->thread_id == sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (self->process));
}
