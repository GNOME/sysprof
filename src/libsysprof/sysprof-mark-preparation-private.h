/* sysprof-mark-preparation.c
 *
 * Copyright 2026 Christian Hergert
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

#include <libdex.h>
#include "sysprof-document-storage-private.h"

G_BEGIN_DECLS

typedef struct
{
  char      *group;
  char      *name;
  EggBitset *bits;
  GArray    *durations;
  gint64     min;
  gint64     max;
  gint64     total;
  gint64     median;
} SysprofPreparedMark;

DexFuture  *_sysprof_mark_prepare         (SysprofDocumentStorage *storage) G_GNUC_WARN_UNUSED_RESULT;
GListModel *_sysprof_mark_prepare_publish (GPtrArray              *prepared,
                                           SysprofDocument        *document);
DexFuture  *_sysprof_mark_prepare_union   (GPtrArray              *prepared,
                                           GArray                 *selected) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
