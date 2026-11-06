/* sysprof-memory-usage-private.h
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#define SYSPROF_MEMORY_USAGE_N_VALUES 17

G_BEGIN_DECLS

void _sysprof_memory_usage_parse_meminfo  (const char *contents,
                                           gint64     *values,
                                           gboolean   *present);
void _sysprof_memory_usage_parse_pressure (const char *contents,
                                           gint64     *values,
                                           gboolean   *present);

G_END_DECLS
