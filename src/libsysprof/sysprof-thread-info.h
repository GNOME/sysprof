/* sysprof-thread-info.h
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

#pragma once

#include "sysprof-document-process.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_THREAD_INFO (sysprof_thread_info_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofThreadInfo, sysprof_thread_info, SYSPROF, THREAD_INFO, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentProcess *sysprof_thread_info_get_process    (SysprofThreadInfo *self);
SYSPROF_AVAILABLE_IN_ALL
int                     sysprof_thread_info_get_thread_id  (SysprofThreadInfo *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean                sysprof_thread_info_is_main_thread (SysprofThreadInfo *self);

G_END_DECLS
