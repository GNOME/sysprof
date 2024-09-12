/*
 * sysprof-allocator-private.h
 *
 * Copysysprofht 2024 Christian Hergert <chergert@redhat.com>
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

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SysprofAllocator SysprofAllocator;

SysprofAllocator *sysprof_allocator_new     (void);
SysprofAllocator *sysprof_allocator_ref     (SysprofAllocator *self);
void              sysprof_allocator_unref   (SysprofAllocator *self);
gpointer          sysprof_allocator_alloc   (SysprofAllocator *self,
                                             gsize             size);
gconstpointer     sysprof_allocator_cstring (SysprofAllocator *self,
                                             const char       *str,
                                             gssize            len);

static inline gconstpointer
sysprof_allocator_cstring_range (SysprofAllocator *self,
                                 const char       *begin,
                                 const char       *end)
{
  return sysprof_allocator_cstring (self, begin, end - begin);
}

static inline gpointer
sysprof_allocator_alloc0 (SysprofAllocator *allocator,
                          gsize             size)
{
  gpointer v = sysprof_allocator_alloc (allocator, size);
  memset (v, 0, size);
  return v;
}

#define sysprof_allocator_new0(a,Type) \
  ((Type*)sysprof_allocator_alloc0(a, sizeof(Type)))

static inline gpointer
sysprof_allocator_dup (SysprofAllocator *allocator,
                       gconstpointer     src,
                       gsize             size)
{
  gpointer dst = sysprof_allocator_alloc (allocator, size);
  memcpy (dst, src, size);
  return dst;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofAllocator, sysprof_allocator_unref);

G_END_DECLS
