/*
 * sysprof-debuginfod-task-private.h
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

#pragma once

#include <elfutils/debuginfod.h>

#include "sysprof-document-task-private.h"
#include "sysprof-elf-private.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DEBUGINFOD_TASK (sysprof_debuginfod_task_get_type())

G_DECLARE_FINAL_TYPE (SysprofDebuginfodTask, sysprof_debuginfod_task, SYSPROF, DEBUGINFOD_TASK, SysprofDocumentTask)

SysprofDebuginfodTask *sysprof_debuginfod_task_new            (void);
SysprofElf            *sysprof_debuginfod_task_find_debuginfo (SysprofDebuginfodTask  *self,
                                                               debuginfod_client      *client,
                                                               const char             *path,
                                                               const char             *build_id_string,
                                                               GError                **error);

G_END_DECLS
