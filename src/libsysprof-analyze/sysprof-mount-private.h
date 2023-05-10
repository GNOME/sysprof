/* sysprof-mount-private.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_MOUNT (sysprof_mount_get_type())

G_DECLARE_FINAL_TYPE (SysprofMount, sysprof_mount, SYSPROF, MOUNT, GObject)

SysprofMount *sysprof_mount_new_for_mountinfo     (const char   *mountinfo);
int           sysprof_mount_get_device_major      (SysprofMount *self);
int           sysprof_mount_get_device_minor      (SysprofMount *self);
const char   *sysprof_mount_get_root              (SysprofMount *self);
const char   *sysprof_mount_get_mount_path        (SysprofMount *self);
const char   *sysprof_mount_get_mount_source      (SysprofMount *self);
const char   *sysprof_mount_get_filesystem_type   (SysprofMount *self);
const char   *sysprof_mount_get_superblock_option (SysprofMount *self,
                                                   const char   *option);

G_END_DECLS
