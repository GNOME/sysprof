/* sp-capture-util.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "sp-capture-util"

#include "config.h"

#include <errno.h>
#include <glib.h>

#ifdef G_OS_WIN32
# include <process.h>
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

#include "sp-capture-util-private.h"

#ifdef G_OS_WIN32
static G_LOCK_DEFINE (_sp_io_sync);
#endif

size_t
(_sp_getpagesize) (void)
{
  static size_t pgsz = 0;

  if G_UNLIKELY (pgsz == 0)
    {
#ifdef G_OS_WIN32
      SYSTEM_INFO system_info;
      GetSystemInfo (&system_info);
      pgsz = system_info.dwPageSize;
#else
      pgsz = getpagesize ();
#endif
    }

  return pgsz;
}

ssize_t
(_sp_pread) (int     fd,
             void   *buf,
             size_t  count,
             off_t   offset)
{
#ifdef G_OS_WIN32
  ssize_t ret = -1;

  G_LOCK (_sp_io_sync);
  errno = 0;
  if (lseek (fd, offset, SEEK_SET) != -1)
    ret = read (fd, buf, count);
  G_UNLOCK (_sp_io_sync);

  return ret;
#else
  errno = 0;
  return pread (fd, buf, count, offset);
#endif
}

ssize_t
(_sp_pwrite) (int         fd,
              const void *buf,
              size_t      count,
              off_t       offset)
{
#ifdef G_OS_WIN32
  ssize_t ret = -1;

  G_LOCK (_sp_io_sync);
  errno = 0;
  if (lseek (fd, offset, SEEK_SET) != -1)
    ret = write (fd, buf, count);
  G_UNLOCK (_sp_io_sync);

  return ret;
#else
  errno = 0;
  return pwrite (fd, buf, count, offset);
#endif
}

ssize_t
(_sp_write) (int         fd,
             const void *buf,
             size_t      count)
{
#ifdef G_OS_WIN32
  ssize_t ret = -1;

  G_LOCK (_sp_io_sync);
  errno = 0;
  ret = write (fd, buf, count);
  G_UNLOCK (_sp_io_sync);

  return ret;
#else
  errno = 0;
  return write (fd, buf, count);
#endif
}

gint32
(_sp_getpid) (void)
{
#ifdef G_OS_WIN32
  return _getpid ();
#else
  return getpid ();
#endif
}

ssize_t
(_sp_sendfile) (int     out_fd,
                int     in_fd,
                off_t  *offset,
                size_t  count)
{
  ssize_t total = 0;
  off_t wpos = 0;
  off_t rpos = 0;

  errno = 0;

  if (offset != NULL && *offset > 0)
    wpos = rpos = *offset;

  while (count > 0)
    {
      unsigned char buf[4096*4];
      ssize_t n_written = 0;
      ssize_t n_read;
      off_t off = 0;
      size_t to_read;

      /* Try to page align */
      if ((rpos % 4096) != 0)
        to_read = 4096 - rpos;
      else
        to_read = sizeof buf;

      if (to_read > count)
        to_read = count;

      errno = 0;
      n_read = _sp_pread (in_fd, buf, to_read, rpos);

      if (n_read <= 0)
        return -1;

      g_assert (count >= n_read);

      count -= n_read;
      rpos += n_read;

      while (wpos < rpos)
        {
          g_assert (off < sizeof buf);

          errno = 0;
          n_written = write (out_fd, &buf[off], rpos - wpos);

          if (n_written <= 0)
            return -1;

          wpos += n_written;
          off += n_written;
          total += n_written;
        }
    }

  g_assert (count == 0);

  if (offset != NULL)
    *offset = rpos;

  errno = 0;
  return total;
}
