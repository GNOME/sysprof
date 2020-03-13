/* sysprof-speedtrack-collector.c
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

#define _GNU_SOURCE

#include "config.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "backtrace-helper.h"

static void    hook_func      (void **addr, const char *name);
static int     hook_open      (const char *filename, int flags, ...);
static int     hook_close     (int fd);
static int     hook_fsync     (int fd);
static int     hook_fdatasync (int fd);
static int     hook_msync     (void *addr, size_t length, int flags);
static ssize_t hook_read      (int fd, void *buf, size_t nbyte);
static ssize_t hook_write     (int fd, const void *buf, size_t nbyte);

static __thread gboolean rec_guard;
static int (*real_open) (const char *filename, int flags, ...) = hook_open;
static int (*real_close) (int fd) = hook_close;
static int (*real_fsync) (int fd) = hook_fsync;
static int (*real_fdatasync) (int fd) = hook_fdatasync;
static int (*real_msync) (void *addr, size_t length, int flags) = hook_msync;
static ssize_t (*real_read) (int fd, void *buf, size_t nbyte) = hook_read;
static ssize_t (*real_write) (int fd, const void *buf, size_t nbyte) = hook_write;

static inline gboolean
is_capturing (void)
{
  static __thread int tid = 0;
  static int pid = 0;

  if (rec_guard)
    return FALSE;

  if G_UNLIKELY (tid == 0)
    tid = syscall (__NR_gettid, 0);

  if G_UNLIKELY (pid == 0)
    pid = getpid ();

  return tid == pid;
}

static void
hook_func (void       **addr,
           const char  *name)
{
  static GRecMutex m;
  static gboolean did_init;

  g_rec_mutex_lock (&m);
  if (!did_init)
    {
      did_init = TRUE;
      backtrace_init ();
    }
  g_rec_mutex_unlock (&m);

  *addr = dlsym (RTLD_NEXT, name);
}

int
open (const char *filename,
      int         flags,
      ...)
{
  va_list args;
  mode_t mode;

  va_start (args, flags);
  mode = va_arg (args, mode_t);
  va_end (args);

  if (is_capturing ())
    {
      gchar str[1024];
      gint64 begin;
      gint64 end;
      int ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_open (filename, flags, mode);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (str, sizeof str,
                  "flags = 0x%x, mode = 0%o, filename = %s => %d",
                  flags, mode, filename, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "open", str);

      rec_guard = FALSE;

      return ret;
    }

  return real_open (filename, flags, mode);
}

static int
hook_open (const char *filename, int flags, ...)
{
  va_list args;
  mode_t mode;

  va_start (args, flags);
  mode = va_arg (args, mode_t);
  va_end (args);

  hook_func ((void **)&real_open, "open");

  return real_open (filename, flags, mode);
}

int
close (int fd)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar fdstr[32];
      int ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_close (fd);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (fdstr, sizeof fdstr, "fd = %d => %d", fd, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "close", fdstr);

      rec_guard = FALSE;

      return ret;
    }

  return real_close (fd);
}

static int
hook_close (int fd)
{
  hook_func ((void **)&real_close, "close");
  return real_close (fd);
}

int
fsync (int fd)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar fdstr[32];
      int ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_fsync (fd);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (fdstr, sizeof fdstr, "fd = %d => %d", fd, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "fsync", fdstr);

      rec_guard = FALSE;

      return ret;
    }

  return real_fsync (fd);
}

static int
hook_fsync (int fd)
{
  hook_func ((void **)&real_fsync, "fsync");
  return real_fsync (fd);
}

int
fdatasync (int fd)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar fdstr[32];
      int ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_fdatasync (fd);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (fdstr, sizeof fdstr, "fd = %d => %d", fd, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "fdatasync", fdstr);

      rec_guard = FALSE;

      return ret;
    }

  return real_fdatasync (fd);
}

static int
hook_fdatasync (int fd)
{
  hook_func ((void **)&real_fdatasync, "fdatasync");
  return real_fdatasync (fd);
}

int
msync (void   *addr,
       size_t  length,
       int     flags)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar str[64];
      int ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_msync (addr, length, flags);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (str, sizeof str, "addr = %p, length = %"G_GSIZE_FORMAT", flags = %d => %d",
                  addr, length, flags, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "msync", str);

      rec_guard = FALSE;

      return ret;
    }

  return real_msync (addr, length, flags);
}

static int
hook_msync (void   *addr,
            size_t  length,
            int     flags)
{
  hook_func ((void **)&real_msync, "msync");
  return real_msync (addr, length, flags);
}

ssize_t
read (int     fd,
      void   *buf,
      size_t  nbyte)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar str[64];
      ssize_t ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_read (fd, buf, nbyte);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (str, sizeof str, "fd = %d, buf = %p, nbyte = %"G_GSIZE_FORMAT" => %"G_GSSIZE_FORMAT,
                  fd, buf, nbyte, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "read", str);

      rec_guard = FALSE;

      return ret;
    }

  return real_read (fd, buf, nbyte);
}

static ssize_t
hook_read (int     fd,
           void   *buf,
           size_t  nbyte)
{
  hook_func ((void **)&real_read, "read");
  return real_read (fd, buf, nbyte);
}

ssize_t
write (int         fd,
       const void *buf,
       size_t      nbyte)
{
  if (is_capturing ())
    {
      gint64 begin;
      gint64 end;
      gchar str[64];
      ssize_t ret;

      rec_guard = TRUE;

      begin = SYSPROF_CAPTURE_CURRENT_TIME;
      ret = real_write (fd, buf, nbyte);
      end = SYSPROF_CAPTURE_CURRENT_TIME;

      g_snprintf (str, sizeof str, "fd = %d, buf = %p, nbyte = %"G_GSIZE_FORMAT" => %"G_GSSIZE_FORMAT,
                  fd, buf, nbyte, ret);
      sysprof_collector_sample (backtrace_func, NULL);
      sysprof_collector_mark (begin, end - begin, "speedtrack", "write", str);

      rec_guard = FALSE;

      return ret;
    }

  return real_write (fd, buf, nbyte);
}

static ssize_t
hook_write (int         fd,
            const void *buf,
            size_t      nbyte)
{
  hook_func ((void **)&real_write, "write");
  return real_write (fd, buf, nbyte);
}
