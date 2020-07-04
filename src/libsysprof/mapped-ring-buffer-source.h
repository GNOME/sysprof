/* mapped-ring-buffer-source.h
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include <glib.h>

#include "sysprof-version-macros.h"

#include "mapped-ring-buffer.h"

G_BEGIN_DECLS

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MappedRingBuffer, mapped_ring_buffer_unref)

G_GNUC_INTERNAL
guint             mapped_ring_buffer_create_source      (MappedRingBuffer         *self,
                                                         MappedRingBufferCallback  callback,
                                                         gpointer                  user_data);
G_GNUC_INTERNAL
guint             mapped_ring_buffer_create_source_full (MappedRingBuffer         *self,
                                                         MappedRingBufferCallback  callback,
                                                         gpointer                  user_data,
                                                         GDestroyNotify            destroy);

G_END_DECLS
