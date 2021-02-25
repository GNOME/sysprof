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

#include <glib.h>

#include "sysprof-capture-types.h"

G_BEGIN_DECLS

typedef struct _SysprofMapOverlay
{
  const char *src;
  const char *dst;
} SysprofMapOverlay;

typedef struct _SysprofMapLookaside
{
  GSequence    *seq;
  GStringChunk *chunk;
  GArray       *overlays;
} SysprofMapLookaside;

typedef struct
{
  SysprofCaptureAddress  start;
  SysprofCaptureAddress  end;
  off_t                  offset;
  ino_t                  inode;
  const gchar           *filename;
} SysprofMap;

SysprofMapLookaside *sysprof_map_lookaside_new      (void);
void                 sysprof_map_lookaside_insert   (SysprofMapLookaside    *self,
                                                     const SysprofMap       *map);
void                 sysprof_map_lookaside_overlay  (SysprofMapLookaside    *self,
                                                     const gchar            *src,
                                                     const gchar            *dst);
const SysprofMap    *sysprof_map_lookaside_lookup   (SysprofMapLookaside    *self,
                                                     SysprofCaptureAddress   address);
void                 sysprof_map_lookaside_free     (SysprofMapLookaside    *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofMapLookaside, sysprof_map_lookaside_free)

G_END_DECLS
