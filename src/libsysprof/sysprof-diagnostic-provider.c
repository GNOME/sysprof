/* sysprof-diagnostic-provider.c
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

#include "config.h"

#include "sysprof-diagnostic-provider-private.h"

G_DEFINE_ABSTRACT_TYPE (SysprofDiagnosticProvider, sysprof_diagnostic_provider, G_TYPE_OBJECT)

static void
sysprof_diagnostic_provider_class_init (SysprofDiagnosticProviderClass *klass)
{
}

static void
sysprof_diagnostic_provider_init (SysprofDiagnosticProvider *self)
{
}

void
_sysprof_diagnostic_provider_populate (SysprofDiagnosticProvider *self,
                                       SysprofDocument           *document,
                                       GListStore                *diagnostics)
{
  g_return_if_fail (SYSPROF_IS_DIAGNOSTIC_PROVIDER (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (G_IS_LIST_STORE (diagnostics));

  SYSPROF_DIAGNOSTIC_PROVIDER_GET_CLASS (self)->populate (self, document, diagnostics);
}
