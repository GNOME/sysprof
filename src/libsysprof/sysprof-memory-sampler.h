/* sysprof-memory-sampler.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include "sysprof-instrument.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MEMORY_SAMPLER         (sysprof_memory_sampler_get_type())
#define SYSPROF_IS_MEMORY_SAMPLER(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_MEMORY_SAMPLER)
#define SYSPROF_MEMORY_SAMPLER(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_MEMORY_SAMPLER, SysprofMemorySampler)
#define SYSPROF_MEMORY_SAMPLER_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_MEMORY_SAMPLER, SysprofMemorySamplerClass)

typedef struct _SysprofMemorySampler      SysprofMemorySampler;
typedef struct _SysprofMemorySamplerClass SysprofMemorySamplerClass;

SYSPROF_AVAILABLE_IN_52
GType              sysprof_memory_sampler_get_type (void);
SYSPROF_AVAILABLE_IN_52
SysprofInstrument *sysprof_memory_sampler_new      (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofMemorySampler, g_object_unref)

G_END_DECLS
