/* sp-callgraph-profile.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#include "sysprof-version-macros.h"

#include "sp-profile.h"
#include "sp-selection.h"

G_BEGIN_DECLS

#define SP_TYPE_CALLGRAPH_PROFILE (sp_callgraph_profile_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpCallgraphProfile, sp_callgraph_profile, SP, CALLGRAPH_PROFILE, GObject)

SYSPROF_AVAILABLE_IN_ALL
SpProfile *sp_callgraph_profile_new                (void);
SYSPROF_AVAILABLE_IN_ALL
SpProfile *sp_callgraph_profile_new_with_selection (SpSelection        *selection);
SYSPROF_AVAILABLE_IN_ALL
gpointer sp_callgraph_profile_get_stash            (SpCallgraphProfile *self);
SYSPROF_AVAILABLE_IN_ALL
GQuark     sp_callgraph_profile_get_tag            (SpCallgraphProfile *self,
                                                    const gchar        *symbol);

G_END_DECLS
