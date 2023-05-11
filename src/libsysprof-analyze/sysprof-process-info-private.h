/* sysprof-process-info-private.h
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

#include "sysprof-address-layout-private.h"
#include "sysprof-mount-namespace-private.h"
#include "sysprof-symbol-cache-private.h"

G_BEGIN_DECLS

typedef struct _SysprofProcessInfo
{
  SysprofAddressLayout  *address_layout;
  SysprofMountNamespace *mount_namespace;
  SysprofSymbolCache    *symbol_cache;
  int                    pid;
} SysprofProcessInfo;

SysprofProcessInfo *sysprof_process_info_new   (SysprofMountNamespace *mount_namespace,
                                                int                    pid);
SysprofProcessInfo *sysprof_process_info_ref   (SysprofProcessInfo    *self);
void                sysprof_process_info_unref (SysprofProcessInfo    *self);

G_END_DECLS
