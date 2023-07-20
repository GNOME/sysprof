/* sysprof-mount-namespace-private.h
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

#include <glib-object.h>

#include "sysprof-mount-private.h"
#include "sysprof-mount-device-private.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MOUNT_NAMESPACE (sysprof_mount_namespace_get_type())

G_DECLARE_FINAL_TYPE (SysprofMountNamespace, sysprof_mount_namespace, SYSPROF, MOUNT_NAMESPACE, GObject)

SysprofMountNamespace  *sysprof_mount_namespace_new         (void);
SysprofMountNamespace  *sysprof_mount_namespace_copy        (SysprofMountNamespace  *self);
void                    sysprof_mount_namespace_add_device  (SysprofMountNamespace  *self,
                                                             SysprofMountDevice     *mount);
void                    sysprof_mount_namespace_add_mount   (SysprofMountNamespace  *self,
                                                             SysprofMount           *mount);
char                  **sysprof_mount_namespace_translate   (SysprofMountNamespace  *self,
                                                             const char             *path);

G_END_DECLS
