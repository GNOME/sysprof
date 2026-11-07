/* sysprof-frame-cycle-private.h
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

#include "sysprof-frame-cycle.h"

G_BEGIN_DECLS

typedef struct _SysprofDocumentReader SysprofDocumentReader;

gboolean           _sysprof_frame_cycle_has_compositor (SysprofFrameCycle     *self);
SysprofFrameCycle *_sysprof_frame_cycle_new_for_reader (SysprofDocumentReader *reader);

G_END_DECLS
