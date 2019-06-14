/* sysprof-governor-source.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-source.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_GOVERNOR_SOURCE (sysprof_governor_source_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofGovernorSource, sysprof_governor_source, SYSPROF, GOVERNOR_SOURCE, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSource *sysprof_governor_source_new                  (void);
SYSPROF_AVAILABLE_IN_ALL
gboolean       sysprof_governor_source_get_disable_governor (SysprofGovernorSource *self);
SYSPROF_AVAILABLE_IN_ALL
void           sysprof_governor_source_set_disable_governor (SysprofGovernorSource *self,
                                                             gboolean               disable_governor);

G_END_DECLS
