/* sysprof-fd-private.h
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <glib-object.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_FD (sysprof_fd_get_type())

typedef struct _SysprofFD SysprofFD;

GType      sysprof_fd_get_type (void) G_GNUC_CONST;
int        sysprof_fd_peek     (const SysprofFD *fd);
int        sysprof_fd_steal    (SysprofFD       *fd);
SysprofFD *sysprof_fd_dup      (const SysprofFD *fd);
void       sysprof_fd_free     (SysprofFD       *fd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofFD, sysprof_fd_free)

G_END_DECLS
