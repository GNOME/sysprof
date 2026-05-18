/* test-collector.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib.h>
#include <sysprof-capture.h>

#ifdef G_OS_UNIX
# include <poll.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

static void
test_collector_is_active_does_not_request_ring (void)
{
#ifdef G_OS_UNIX
  struct pollfd pfd;
  char fdstr[16];
  int fds[2];

  g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);

  g_snprintf (fdstr, sizeof fdstr, "%d", fds[0]);
  g_setenv ("SYSPROF_CONTROL_FD", fdstr, TRUE);

  g_assert_true (sysprof_collector_is_active ());

  pfd.fd = fds[1];
  pfd.events = POLLIN;
  pfd.revents = 0;

  g_assert_cmpint (poll (&pfd, 1, 0), ==, 0);

  close (fds[0]);
  close (fds[1]);
  g_unsetenv ("SYSPROF_CONTROL_FD");
#else
  g_test_skip ("Control FD collection is only supported on Unix");
#endif
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Sysprof/Collector/is-active-no-ring",
                   test_collector_is_active_does_not_request_ring);
  return g_test_run ();
}
