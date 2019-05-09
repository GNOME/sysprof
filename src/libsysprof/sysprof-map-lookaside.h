/* sysprof-map-lookaside.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include "sysprof-capture-types.h"

G_BEGIN_DECLS

typedef struct _SysprofMapLookaside SysprofMapLookaside;

typedef struct
{
  SysprofCaptureAddress  start;
  SysprofCaptureAddress  end;
  off_t             offset;
  ino_t             inode;
  const gchar      *filename;
} SysprofMap;

SYSPROF_AVAILABLE_IN_ALL
SysprofMapLookaside *sysprof_map_lookaside_new    (void);
void                 sysprof_map_lookaside_insert (SysprofMapLookaside   *self,
                                                   const SysprofMap      *map);
SYSPROF_AVAILABLE_IN_ALL
const SysprofMap    *sysprof_map_lookaside_lookup (SysprofMapLookaside   *self,
                                                   SysprofCaptureAddress  address);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_map_lookaside_free   (SysprofMapLookaside   *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofMapLookaside, sysprof_map_lookaside_free)

G_END_DECLS
