/* sysprof-memprof-profile.h
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-version-macros.h"

#include "sysprof-profile.h"
#include "sysprof-selection.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MEMPROF_PROFILE (sysprof_memprof_profile_get_type())

typedef enum
{
  SYSPROF_MEMPROF_MODE_SUMMARY = 0,
  SYSPROF_MEMPROF_MODE_ALL_ALLOCS = 1,
  SYSPROF_MEMPROF_MODE_TEMP_ALLOCS = 2,
} SysprofMemprofMode;

typedef struct
{
  gint64 n_allocs;
  gint64 leaked_allocs;
  gint64 leaked_allocs_size;
  gint64 temp_allocs;
  gint64 temp_allocs_size;
  struct {
    gint64 bucket;
    gint64 n_allocs;
    gint64 temp_allocs;
    gint64 allocated;
  } by_size[14];
  /*< private >*/
  gint64 _reserved[32];
} SysprofMemprofStats;

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofMemprofProfile, sysprof_memprof_profile, SYSPROF, MEMPROF_PROFILE, GObject)

SYSPROF_AVAILABLE_IN_3_36
SysprofProfile     *sysprof_memprof_profile_new                (void);
SYSPROF_AVAILABLE_IN_3_36
SysprofProfile     *sysprof_memprof_profile_new_with_selection (SysprofSelection      *selection);
SYSPROF_AVAILABLE_IN_3_36
void                sysprof_memprof_profile_get_stats          (SysprofMemprofProfile *self,
                                                                SysprofMemprofStats   *stats);
SYSPROF_AVAILABLE_IN_3_36
SysprofMemprofMode  sysprof_memprof_profile_get_mode           (SysprofMemprofProfile *self);
SYSPROF_AVAILABLE_IN_3_36
void                sysprof_memprof_profile_set_mode           (SysprofMemprofProfile *self,
                                                                SysprofMemprofMode     mode);
SYSPROF_AVAILABLE_IN_3_36
gpointer            sysprof_memprof_profile_get_native         (SysprofMemprofProfile *self);
SYSPROF_AVAILABLE_IN_3_36
gpointer            sysprof_memprof_profile_get_stash          (SysprofMemprofProfile *self);
SYSPROF_AVAILABLE_IN_3_36
gboolean            sysprof_memprof_profile_is_empty           (SysprofMemprofProfile *self);
SYSPROF_AVAILABLE_IN_3_36
GQuark              sysprof_memprof_profile_get_tag            (SysprofMemprofProfile *self,
                                                                const gchar           *symbol);

G_END_DECLS
