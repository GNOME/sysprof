/* sysprof-proxied-instrument.h
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

#include <gio/gio.h>

#include "sysprof-instrument.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROXIED_INSTRUMENT         (sysprof_proxied_instrument_get_type())
#define SYSPROF_IS_PROXIED_INSTRUMENT(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_PROXIED_INSTRUMENT)
#define SYSPROF_PROXIED_INSTRUMENT(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_PROXIED_INSTRUMENT, SysprofProxiedInstrument)
#define SYSPROF_PROXIED_INSTRUMENT_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_PROXIED_INSTRUMENT, SysprofProxiedInstrumentClass)

typedef struct _SysprofProxiedInstrument      SysprofProxiedInstrument;
typedef struct _SysprofProxiedInstrumentClass SysprofProxiedInstrumentClass;

SYSPROF_AVAILABLE_IN_ALL
GType              sysprof_proxied_instrument_get_type (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofInstrument *sysprof_proxied_instrument_new              (GBusType    bus_type,
                                                                const char *bus_name,
                                                                const char *object_path);
SYSPROF_AVAILABLE_IN_49
SysprofInstrument *sysprof_proxied_instrument_new_with_options (GBusType    bus_type,
                                                                const char *bus_name,
                                                                const char *object_path,
                                                                GVariant   *options);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofProxiedInstrument, g_object_unref)

G_END_DECLS
