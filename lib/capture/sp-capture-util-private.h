/* sp-capture-util-private.h
 *
 * Copyright © 2019 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SP_CAPTURE_UTIL_PRIVATE_H
#define SP_CAPTURE_UTIL_PRIVATE_H

#include <glib.h>

#ifdef __linux__
# include <sys/sendfile.h>
#endif
#include <errno.h>
#include <unistd.h>

#ifdef __linux__
# define _sp_sendfile sendfile
#else
static inline ssize_t
_sp_sendfile (int     out_fd,
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
      n_read = pread (in_fd, buf, to_read, rpos);

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
#endif

#endif /* SP_CAPTURE_UTIL_PRIVATE_H */
