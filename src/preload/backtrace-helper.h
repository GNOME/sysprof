/* backtrace-helper.h
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

#pragma once

#define UNW_LOCAL_ONLY
#include <libunwind.h>

static void
backtrace_init (void)
{
  unw_set_caching_policy (unw_local_addr_space, UNW_CACHE_PER_THREAD);
#ifdef HAVE_UNW_SET_CACHE_SIZE
  unw_set_cache_size (unw_local_addr_space, 1024, 0);
#endif
}

static int
backtrace_func (SysprofCaptureAddress  *addrs,
                guint                   n_addrs,
                G_GNUC_UNUSED gpointer  user_data)
{
#if GLIB_SIZEOF_VOID_P == 8
  SYSPROF_STATIC_ASSERT (offsetof (SysprofCaptureAllocation, addrs) >= GLIB_SIZEOF_VOID_P * 2,
                         "Require space for 2 discarded instruction pointers");
  /* We know that collector will overwrite fields *AFTER* it
   * has called the backtrace function allowing us to cheat
   * and subtract an offset from addrs to avoid having to
   * copy frame pointers around.
   */
  return unw_backtrace (((void **)addrs) - 2, n_addrs) - 2;
#else
  static const int skip = 2;
  void **stack = alloca (n_addrs * sizeof (gpointer));
  int n = unw_backtrace (stack, n_addrs);
  for (guint i = skip; i < n; i++)
    addrs[i-skip] = GPOINTER_TO_SIZE (stack[i]);
  return MAX (0, n - skip);
#endif
}

