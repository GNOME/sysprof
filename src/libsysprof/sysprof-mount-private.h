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

#include "sysprof-mount.h"
#include "sysprof-strings-private.h"

G_BEGIN_DECLS

struct _SysprofMount
{
  GObject parent_instance;
  int mount_id;
  int parent_mount_id;
  int device_major;
  int device_minor;
  GRefString *root;
  GRefString *mount_point;
  GRefString *mount_source;
  GRefString *filesystem_type;
  GRefString *superblock_options;
  guint is_overlay : 1;
  guint layer : 15;
};

SysprofMount *_sysprof_mount_new_for_mountinfo (SysprofStrings *strings,
                                                const char     *mountinfo);
SysprofMount *_sysprof_mount_new_for_overlay   (SysprofStrings *strings,
                                                const char     *mount_point,
                                                const char     *host_path,
                                                int             layer);
const char   *_sysprof_mount_get_relative_path (SysprofMount   *self,
                                                const char     *path);

G_END_DECLS
