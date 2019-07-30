/* sysprof-turbostat.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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
 * SPDX-License-ntifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "sysprof-turbostat.h"

#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
# include <sys/prctl.h>
#endif

#define PTY_FD_INVALID (-1)

typedef int PtyFd;

struct _SysprofTurbostat
{
  GPid        pid;
  GIOChannel *stdin;
  GIOChannel *stdout;
};

enum {
  KIND_0,
  KIND_DOUBLE,
  KIND_INT,
};

static inline PtyFd
pty_fd_steal (PtyFd *fd)
{
  PtyFd ret = *fd;
  *fd = -1;
  return ret;
}

static void
pty_fd_clear (PtyFd *fd)
{
  if (fd != NULL && *fd != -1)
    {
      int rfd = *fd;
      *fd = -1;
      close (rfd);
    }
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (PtyFd, pty_fd_clear)

PtyFd
pty_create_slave (PtyFd    master_fd,
                  gboolean blocking)
{
  g_auto(PtyFd) ret = PTY_FD_INVALID;
  gint extra = blocking ? 0 : O_NONBLOCK;
#if defined(HAVE_PTSNAME_R) || defined(__FreeBSD__)
  char name[256];
#else
  const char *name;
#endif

  g_assert (master_fd != -1);

  if (grantpt (master_fd) != 0)
    return PTY_FD_INVALID;

  if (unlockpt (master_fd) != 0)
    return PTY_FD_INVALID;

#ifdef HAVE_PTSNAME_R
  if (ptsname_r (master_fd, name, sizeof name - 1) != 0)
    return PTY_FD_INVALID;
  name[sizeof name - 1] = '\0';
#elif defined(__FreeBSD__)
  if (fdevname_r (master_fd, name + 5, sizeof name - 6) == NULL)
    return PTY_FD_INVALID;
  memcpy (name, "/dev/", 5);
  name[sizeof name - 1] = '\0';
#else
  if (NULL == (name = ptsname (master_fd)))
    return PTY_FD_INVALID;
#endif

  ret = open (name, O_NOCTTY | O_RDWR | O_CLOEXEC | extra);

  if (ret == PTY_FD_INVALID && errno == EINVAL)
    {
      gint flags;

      ret = open (name, O_NOCTTY | O_RDWR | O_CLOEXEC);
      if (ret == PTY_FD_INVALID && errno == EINVAL)
        ret = open (name, O_NOCTTY | O_RDWR);

      if (ret == PTY_FD_INVALID)
        return PTY_FD_INVALID;

      /* Add FD_CLOEXEC if O_CLOEXEC failed */
      flags = fcntl (ret, F_GETFD, 0);
      if ((flags & FD_CLOEXEC) == 0)
        {
          if (fcntl (ret, F_SETFD, flags | FD_CLOEXEC) < 0)
            return PTY_FD_INVALID;
        }

      if (!blocking)
        {
          if (!g_unix_set_fd_nonblocking (ret, TRUE, NULL))
            return PTY_FD_INVALID;
        }
    }

  return pty_fd_steal (&ret);
}

PtyFd
pty_create_master (void)
{
  g_auto(PtyFd) master_fd = PTY_FD_INVALID;

  master_fd = posix_openpt (O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

#ifndef __linux__
  /* Fallback for operating systems that don't support
   * O_NONBLOCK and O_CLOEXEC when opening.
   */
  if (master_fd == PTY_FD_INVALID && errno == EINVAL)
    {
      master_fd = posix_openpt (O_RDWR | O_NOCTTY | O_CLOEXEC);

      if (master_fd == PTY_FD_INVALID && errno == EINVAL)
        {
          gint flags;

          master_fd = posix_openpt (O_RDWR | O_NOCTTY);
          if (master_fd == -1)
            return PTY_FD_INVALID;

          flags = fcntl (master_fd, F_GETFD, 0);
          if (flags < 0)
            return PTY_FD_INVALID;

          if (fcntl (master_fd, F_SETFD, flags | FD_CLOEXEC) < 0)
            return PTY_FD_INVALID;
        }

      if (!g_unix_set_fd_nonblocking (master_fd, TRUE, NULL))
        return PTY_FD_INVALID;
    }
#endif

  return pty_fd_steal (&master_fd);
}

SysprofTurbostat *
sysprof_turbostat_new (void)
{
  SysprofTurbostat *self;

  self = g_rc_box_new0 (SysprofTurbostat);
  self->stdin = NULL;
  self->stdout = NULL;
  self->pid = 0;

  return g_steal_pointer (&self);
}

static void
sysprof_turbostat_finalize (gpointer data)
{
  SysprofTurbostat *self = data;

  if (self->pid != 0)
    sysprof_turbostat_stop (self);

  g_assert (self->pid == 0);
  g_assert (self->stdin == NULL);
  g_assert (self->stdout == NULL);
}

void
sysprof_turbostat_free (SysprofTurbostat *self)
{
  g_rc_box_release_full (self, sysprof_turbostat_finalize);
}

static void
child_setup_cb (gpointer data)
{
#ifdef __linux__
  prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif
}

gboolean
sysprof_turbostat_start (SysprofTurbostat  *self,
                         GError           **error)
{
  /* We use a long interval and just send \n to force a sample */
  static const gchar *argv[] = { "turbostat", "-T", "Celcius", "-i", "100000", NULL };
  g_auto(GStrv) env = NULL;
  g_auto(PtyFd) stdin_master = PTY_FD_INVALID;
  g_auto(PtyFd) stdin_slave = PTY_FD_INVALID;
  g_auto(PtyFd) stdout_read = PTY_FD_INVALID;
  g_auto(PtyFd) stdout_write = PTY_FD_INVALID;
  gint pipes[2];
  gboolean ret;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (self->stdin == NULL, FALSE);
  g_return_val_if_fail (self->stdout == NULL, FALSE);

  env = g_get_environ ();
  env = g_environ_setenv (env, "LANG", "C", TRUE);

  if (-1 == (stdin_master = pty_create_master ()) ||
      -1 == (stdin_slave = pty_create_slave (stdin_master, FALSE)) ||
      0 != pipe2 (pipes, O_CLOEXEC | O_NONBLOCK))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return FALSE;
    }

  stdout_read = pipes[0];
  stdout_write = pipes[1];

  ret = g_spawn_async_with_fds (NULL,
                                (gchar **)argv,
                                env,
                                (G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL),
                                child_setup_cb,
                                NULL,
                                &self->pid,
                                stdin_slave,
                                stdout_write,
                                -1,
                                error);

  if (ret)
    {
      self->stdin = g_io_channel_unix_new (pty_fd_steal (&stdin_master));
      g_io_channel_set_close_on_unref (self->stdin, TRUE);
      g_io_channel_set_buffer_size (self->stdin, 4096);

      self->stdout = g_io_channel_unix_new (pty_fd_steal (&stdout_read));
      g_io_channel_set_close_on_unref (self->stdout, TRUE);
      g_io_channel_set_buffer_size (self->stdout, 4096);
      g_io_channel_set_flags (self->stdout, G_IO_FLAG_NONBLOCK, NULL);
    }

  return ret;
}

void
sysprof_turbostat_stop (SysprofTurbostat *self)
{
  GPid pid;

  g_return_if_fail (self != NULL);

  if (self->pid == 0)
    return;

  pid = self->pid;
  self->pid = 0;
  kill (pid, SIGTERM);

  g_clear_pointer (&self->stdin, g_io_channel_unref);
  g_clear_pointer (&self->stdout, g_io_channel_unref);
}

GArray *
sysprof_turbostat_sample (SysprofTurbostat  *self,
                          GError           **error)
{
  g_autoptr(GArray) ret = NULL;
  g_auto(GStrv) columns = NULL;
  g_autoptr(GString) str = NULL;
  GIOStatus r;
  gint lineno = 0;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->stdin != NULL, NULL);
  g_return_val_if_fail (self->stdout != NULL, NULL);

  r = g_io_channel_write_chars (self->stdin, "\n", 1, NULL, error) &&
      g_io_channel_flush (self->stdin, error);
  if (r != G_IO_STATUS_NORMAL)
    return NULL;

  /* Sleep for just a bit to wait for all results */
  g_usleep (G_USEC_PER_SEC * 0.01);

  ret = g_array_new (FALSE, FALSE, sizeof (SysprofTurbostatSample));
  str = g_string_new (NULL);

  for (;;)
    {
      SysprofTurbostatSample sample = {0};
      g_auto(GStrv) parts = NULL;
      gsize pos = 0;

      lineno++;

      r = g_io_channel_read_line_string (self->stdout, str, &pos, NULL);
      if (r != G_IO_STATUS_NORMAL || str->len == 0 || pos == 0)
        break;

      g_string_truncate (str, pos - 1);

      parts = g_strsplit (str->str, "\t", 0);

      if (lineno == 1)
        {
          columns = g_steal_pointer (&parts);
          continue;
        }

      g_return_val_if_fail (columns != NULL, NULL);

      for (guint i = 0; columns[i] != NULL && parts[i] != NULL; i++)
        {
          gdouble *addr = NULL;
          gint *iaddr = NULL;
          int kind = KIND_0;

          if (g_strcmp0 (columns[i], "GFXWatt") == 0)
            {
              addr = &sample.gfx_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "CorWatt") == 0)
            {
              addr = &sample.core_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "PkgWatt") == 0)
            {
              addr = &sample.pkg_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "RAMWatt") == 0)
            {
              addr = &sample.ram_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "Core") == 0)
            {
              iaddr = &sample.core;
              kind = KIND_INT;
            }
          else if (g_strcmp0 (columns[i], "CPU") == 0)
            {
              iaddr = &sample.cpu;
              kind = KIND_INT;
            }

          if (kind == KIND_0)
            continue;

          g_assert ((kind == KIND_DOUBLE && addr != NULL) ||
                    (kind == KIND_INT && iaddr != NULL));

          if (kind == KIND_DOUBLE)
            {
              *addr = g_ascii_strtod (parts[i], NULL);

              /* Reset to zero if we failed to parse */
              if (*addr == HUGE_VAL && errno == ERANGE)
                *addr = 0.0;
            }
          else if (kind == KIND_INT)
            {
              /* Some columns are "-" and we use -1 to indicate that. */
              if (parts[i][0] == '-' && parts[i][1] == 0)
                {
                  *iaddr = -1;
                }
              else
                {
                  gint64 v = g_ascii_strtoll (parts[i], NULL, 10);

                  /* Reset if we failed to parse */
                  if ((v == G_MAXINT64 || v == G_MININT64) && errno == ERANGE)
                    v = 0;

                  *iaddr = v;
                }
            }
        }

      g_array_append_val (ret, sample);
    }

  return g_steal_pointer (&ret);
}
