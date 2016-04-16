/* sp-platform.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sp-platform.h"

/**
 * sp_memfd_create:
 * @name: (nullable): A descriptive name for the memfd or %NULL
 *
 * Creates a new memfd using the memfd_create syscall if available.
 * Otherwise, a tmpfile is used. Currently, no attempt is made to
 * ensure the tmpfile is on a tmpfs backed mount.
 *
 * Returns: An fd if successful; otherwise -1 and errno is set.
 */
int
sp_memfd_create (const gchar *name)
{
#ifdef __NR_memfd_create
  if (name == NULL)
    name = "[sysprof]";
  return syscall (__NR_memfd_create, name, 0);
#else
  gchar *name_used = NULL;
  int fd;

  /*
   * TODO: It would be nice to ensure tmpfs
   *
   * It is not strictly required that the preferred temporary directory
   * will be mounted as tmpfs. We should look through the mounts and ensure
   * that the tmpfile we open is on tmpfs so that we get anonymous backed
   * pages after unlinking.
   */
  fd = g_file_open_tmp (NULL, &name_used, NULL);

  if (name_used != NULL)
    {
      g_unlink (name_used);
      g_free (name_used)
    }

  return fd;
#endif
}
