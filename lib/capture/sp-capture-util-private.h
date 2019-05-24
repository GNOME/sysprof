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

#include <unistd.h>

#ifdef __linux__
# define _sp_getpagesize getpagesize
# define _sp_pread       pread
# define _sp_pwrite      pwrite
# define _sp_write       write
# define _sp_getpid      getpid
# define _sp_sendfile    sendfile
#else
size_t  _sp_getpagesize (void);
ssize_t _sp_pread       (int     fd,
                         void   *buf,
                         size_t  count,
                         off_t   offset);
ssize_t _sp_pwrite      (int         fd,
                         const void *buf,
                         size_t      count,
                         off_t       offset);
ssize_t _sp_write       (int         fd,
                         const void *buf,
                         size_t      count);
gint32  _sp_getpid      (void);
ssize_t _sp_sendfile    (int     out_fd,
                         int     in_fd,
                         off_t  *offset,
                         size_t  count);
#endif

#endif /* SP_CAPTURE_UTIL_PRIVATE_H */