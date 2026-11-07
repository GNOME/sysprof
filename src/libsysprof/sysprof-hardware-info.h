/* sysprof-hardware-info.h
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

#include "sysprof-types.h"
#include "sysprof-instrument.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_HARDWARE_INFO (sysprof_hardware_info_get_type())

SYSPROF_AVAILABLE_IN_52
SYSPROF_DECLARE_INTERNAL_TYPE (SysprofHardwareInfo, sysprof_hardware_info, SYSPROF, HARDWARE_INFO, SysprofInstrument)

SYSPROF_AVAILABLE_IN_52
gboolean           sysprof_hardware_info_get_detailed (SysprofHardwareInfo *self);
SYSPROF_AVAILABLE_IN_52
SysprofInstrument *sysprof_hardware_info_new          (void);
SYSPROF_AVAILABLE_IN_52
void               sysprof_hardware_info_set_detailed (SysprofHardwareInfo *self,
                                                       gboolean             detailed);

G_END_DECLS
