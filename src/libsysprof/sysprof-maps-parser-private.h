/* sysprof-maps-parser-private.h
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

#include <glib.h>

#include "line-reader-private.h"

G_BEGIN_DECLS

typedef struct _SysprofMapsParser
{
  LineReader reader;
} SysprofMapsParser;

void     sysprof_maps_parser_init (SysprofMapsParser *self,
                                   const char         *str,
                                   gssize              len);
gboolean sysprof_maps_parser_next (SysprofMapsParser  *self,
                                   guint64            *out_begin_addr,
                                   guint64            *out_end_addr,
                                   guint64            *out_offset,
                                   guint64            *out_inode,
                                   char              **out_filename);

G_END_DECLS
