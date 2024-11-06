/* sysprof-fd.c
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

#include "config.h"

#include <unistd.h>

#include "sysprof-fd-private.h"

int
sysprof_fd_peek (const SysprofFD *fd)
{
  if (fd == NULL)
    return -1;

  return *(int *)fd;
}

int
sysprof_fd_steal (SysprofFD *fd)
{
  if (fd == NULL)
    return -1;

  return g_steal_fd ((int *)fd);
}

void
sysprof_fd_free (SysprofFD *fd)
{
  int real = sysprof_fd_steal (fd);
  if (real != -1)
    close (real);
  g_free (fd);
}

SysprofFD *
sysprof_fd_dup (const SysprofFD *fd)
{
  int real = sysprof_fd_peek (fd);

  if (real == -1)
    return NULL;

  real = dup (real);

  return g_memdup2 (&real, sizeof real);
}

G_DEFINE_BOXED_TYPE (SysprofFD, sysprof_fd, sysprof_fd_dup, sysprof_fd_free)
