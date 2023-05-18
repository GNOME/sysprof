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

#include "sysprof-document-frame-private.h"
#include "sysprof-document-process-private.h"
#include "sysprof-mount.h"

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

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_process_init (SysprofDocumentProcess *self)
{
}

const char *
sysprof_document_process_get_command_line (SysprofDocumentProcess *self)
{
  const SysprofCaptureProcess *proc;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS (self), 0);

  proc = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureProcess);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, proc->cmdline);
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
