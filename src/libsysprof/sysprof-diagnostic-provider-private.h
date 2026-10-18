/* sysprof-diagnostic-provider-private.h
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

#include <gio/gio.h>

#include "sysprof-diagnostic.h"
#include "sysprof-document.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DIAGNOSTIC_PROVIDER (sysprof_diagnostic_provider_get_type())
#define SYSPROF_DIAGNOSTIC_PROVIDER_MAX_PER_TYPE 1000

G_DECLARE_DERIVABLE_TYPE (SysprofDiagnosticProvider, sysprof_diagnostic_provider, SYSPROF, DIAGNOSTIC_PROVIDER, GObject)

struct _SysprofDiagnosticProviderClass
{
  GObjectClass parent_class;

  void (*populate) (SysprofDiagnosticProvider *self,
                    SysprofDocument           *document,
                    GListStore                *diagnostics);
};

void _sysprof_diagnostic_provider_populate (SysprofDiagnosticProvider *self,
                                            SysprofDocument           *document,
                                            GListStore                *diagnostics);

G_END_DECLS
