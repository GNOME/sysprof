/* sysprof-path-resolver.h
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

typedef struct _SysprofPathResolver SysprofPathResolver;

SysprofPathResolver *_sysprof_path_resolver_new         (const char          *mounts,
                                                         const char          *mountinfo);
void                 _sysprof_path_resolver_add_overlay (SysprofPathResolver *self,
                                                         const char          *in_process,
                                                         const char          *on_host,
                                                         int                  depth);
void                 _sysprof_path_resolver_free        (SysprofPathResolver *self);
char                *_sysprof_path_resolver_resolve     (SysprofPathResolver *self,
                                                         const char          *path);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofPathResolver, _sysprof_path_resolver_free)

G_END_DECLS
