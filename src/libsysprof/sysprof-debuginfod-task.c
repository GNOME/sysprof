/*
 * sysprof-debuginfod-task.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <gio/gio.h>

#include "sysprof-debuginfod-task-private.h"

struct _SysprofDebuginfodTask
{
  SysprofDocumentTask parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofDebuginfodTask, sysprof_debuginfod_task, SYSPROF_TYPE_DOCUMENT_TASK)

static void
sysprof_debuginfod_task_class_init (SysprofDebuginfodTaskClass *klass)
{
}

static void
sysprof_debuginfod_task_init (SysprofDebuginfodTask *self)
{
}

SysprofDebuginfodTask *
sysprof_debuginfod_task_new (void)
{
  return g_object_new (SYSPROF_TYPE_DEBUGINFOD_TASK, NULL);
}

static int
sysprof_debuginfod_task_progress_cb (debuginfod_client *client,
                                     long               a,
                                     long               b)
{
  SysprofDocumentTask *task = debuginfod_get_user_data (client);
  double progress;

  g_assert (client != NULL);
  g_assert (SYSPROF_IS_DEBUGINFOD_TASK (task));

  if (b > 0)
    progress = (double)a / (double)b;
  else
    progress = 0;

  _sysprof_document_task_set_progress (task, progress);

  if (sysprof_document_task_is_cancelled (task))
    return -1;

  return 0;
}

SysprofElf *
sysprof_debuginfod_task_find_debuginfo (SysprofDebuginfodTask  *self,
                                        debuginfod_client      *client,
                                        const char             *path,
                                        const char             *build_id_string,
                                        GError                **error)
{
  g_autoptr(GMappedFile) mapped_file = NULL;
  g_autoptr(SysprofElf) debuginfo_elf = NULL;
  g_autofd int fd = -1;
  char *debuginfo_path = NULL;

  g_return_val_if_fail (SYSPROF_IS_DEBUGINFOD_TASK (self), NULL);
  g_return_val_if_fail (client != NULL, NULL);
  g_return_val_if_fail (build_id_string != NULL, NULL);

  debuginfod_set_user_data (client, self);
  debuginfod_set_progressfn (client, sysprof_debuginfod_task_progress_cb);

  _sysprof_document_task_set_title (SYSPROF_DOCUMENT_TASK (self), _("Downloading Symbols…"));
  _sysprof_document_task_take_message (SYSPROF_DOCUMENT_TASK (self), g_strdup (path));

  fd = debuginfod_find_debuginfo (client,
                                  (const unsigned char *)build_id_string, 0,
                                  &debuginfo_path);

  if (fd < 0)
    {
      if (error != NULL)
        {
          int errsv = errno;
          g_set_error_literal (error,
                               G_IO_ERROR,
                               g_io_error_from_errno (errsv),
                               g_strerror (errsv));
        }

      goto failure;
    }

  if (!(mapped_file = g_mapped_file_new_from_fd (fd, FALSE, error)))
    goto failure;

  if (!(debuginfo_elf = sysprof_elf_new (debuginfo_path, g_steal_pointer (&mapped_file), 0, error)))
    goto failure;

failure:
  free (debuginfo_path);

  debuginfod_set_user_data (client, NULL);
  debuginfod_set_progressfn (client, NULL);

  return g_steal_pointer (&debuginfo_elf);
}
