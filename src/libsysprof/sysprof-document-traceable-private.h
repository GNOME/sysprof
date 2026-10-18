/* sysprof-document-traceable-private.h
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

#include <libdex.h>

#include "sysprof-document-traceable.h"

G_BEGIN_DECLS

DexFuture *_sysprof_document_traceable_resolve_source_location (SysprofDocumentTraceable *self,
                                                                guint                     position,
                                                                const char * const       *debug_dirs,
                                                                const char * const       *external_debug_dirs) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
